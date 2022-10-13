#include "Game.hpp"

#include "Connection.hpp"
#include "LitColorTextureProgram.hpp"
#include "DrawLines.hpp"
#include "Mesh.hpp"
#include "Load.hpp"
#include "gl_errors.hpp"
#include "data_path.hpp"
#include "WalkMesh.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/norm.hpp>
#include <random>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <cstring>

GLuint meshes_for_lit_color_texture_program = 0;
Load< MeshBuffer > playground_meshes(LoadTagDefault, []() -> MeshBuffer const * {
	MeshBuffer const *ret = new MeshBuffer(data_path("playground.pnct"));
	meshes_for_lit_color_texture_program = ret->make_vao_for_program(lit_color_texture_program->program);
	return ret;
});

Load< Scene > playground_scene(LoadTagDefault, []() -> Scene const * {
	return new Scene(data_path("playground.scene"), [&](Scene &scene, Scene::Transform *transform, std::string const &mesh_name){
		Mesh const &mesh = playground_meshes->lookup(mesh_name);

		scene.drawables.emplace_back(transform);
		Scene::Drawable &drawable = scene.drawables.back();

		drawable.pipeline = lit_color_texture_program_pipeline;

		drawable.pipeline.vao = meshes_for_lit_color_texture_program;
		drawable.pipeline.type = mesh.type;
		drawable.pipeline.start = mesh.start;
		drawable.pipeline.count = mesh.count;
	});
});

void Player::Controls::send_controls_message(Connection *connection_) const {
	assert(connection_);
	auto &connection = *connection_;

	uint32_t size = 5;
	connection.send(Message::C2S_Controls);
	connection.send(uint8_t(size));
	connection.send(uint8_t(size >> 8));
	connection.send(uint8_t(size >> 16));
	connection.send(uint8_t(size >> 24));

	auto send_button = [&](Button const &b) {
		if (b.downs & 0x80) {
			std::cerr << "Wow, you are really good at pressing buttons!" << std::endl;
		}
		connection.send(uint8_t( (b.pressed ? 0x80 : 0x00) | (b.downs & 0x7f) ) );
	};

	send_button(left);
	send_button(right);
	send_button(up);
	send_button(down);
}

bool Player::Controls::recv_controls_message(Connection *connection_) {
	assert(connection_);
	auto &connection = *connection_;

	auto &recv_buffer = connection.recv_buffer;

	//expecting [type, size_low0, size_mid8, size_high8]:
	if (recv_buffer.size() < 5) return false;
	if (recv_buffer[0] != uint8_t(Message::C2S_Controls)) return false;
	uint32_t size = (uint32_t(recv_buffer[4]) << 24)
				  | (uint32_t(recv_buffer[3]) << 16)
	              | (uint32_t(recv_buffer[2]) << 8)
	              |  uint32_t(recv_buffer[1]);
	if (size != 4) throw std::runtime_error("Controls message with size " + std::to_string(size) + " != 4!");
	
	//expecting complete message:
	if (recv_buffer.size() < 5 + size) return false;

	auto recv_button = [](uint8_t byte, Button *button) {
		button->pressed = (byte & 0x80);
		uint32_t d = uint32_t(button->downs) + uint32_t(byte & 0x7f);
		if (d > 255) {
			std::cerr << "got a whole lot of downs" << std::endl;
			d = 255;
		}
		button->downs = uint8_t(d);
	};

	recv_button(recv_buffer[5+0], &left);
	recv_button(recv_buffer[5+1], &right);
	recv_button(recv_buffer[5+2], &up);
	recv_button(recv_buffer[5+3], &down);

	//delete message from buffer:
	recv_buffer.erase(recv_buffer.begin(), recv_buffer.begin() + 5 + size);

	return true;
}


//-----------------------------------------

Game::Game() : scene(*playground_scene) {
	for (auto &transform : scene.transforms) {
		if (transform.name == "Player1") this->player1.transform = &transform;
		if (transform.name == "Player2") this->player2.transform = &transform;
	}

	//get pointer to camera for convenience:
	if (scene.cameras.size() != 1) throw std::runtime_error("Expecting scene to have exactly one camera, but it has " + std::to_string(scene.cameras.size()));
	camera = &scene.cameras.front();
}

Player *Game::spawn_player() {
	Player *player;
	if (!player1.active) {
		player1.active = true;
		player = &player1;
	} else if (!player2.active) {
		player2.active = true;
		player = &player2;
	} else {
		return nullptr;
	}

	return player;
}

void Game::remove_player(Player *player) {
	bool found = false;
	if (&player1 == player) {
		found = true;
		player1.active = false;
	} else if (&player2 == player) {
		found = true;
		player2.active = false;
	}
	assert(found);
}

void Game::update(float elapsed) {
	// player1
	{
		Player &p = player1;
		glm::vec3 dir = glm::vec3(0.0f, 0.0f, 0.0f);
		if (p.controls.left.pressed) dir.x -= 1.0f;
		if (p.controls.right.pressed) dir.x += 1.0f;
		if (p.controls.down.pressed) dir.y -= 1.0f;
		if (p.controls.up.pressed) dir.y += 1.0f;

		p.transform->position += glm::normalize(dir * elapsed);
		
		p.controls.left.downs = 0;
		p.controls.right.downs = 0;
		p.controls.up.downs = 0;
		p.controls.down.downs = 0;
	}
	{
		Player &p = player2;
		glm::vec3 dir = glm::vec3(0.0f, 0.0f, 0.0f);
		if (p.controls.left.pressed) dir.x -= 1.0f;
		if (p.controls.right.pressed) dir.x += 1.0f;
		if (p.controls.down.pressed) dir.y -= 1.0f;
		if (p.controls.up.pressed) dir.y += 1.0f;

		p.transform->position += glm::normalize(dir * elapsed);

		p.controls.left.downs = 0;
		p.controls.right.downs = 0;
		p.controls.up.downs = 0;
		p.controls.down.downs = 0;
	}
}


void Game::send_state_message(Connection *connection_, Player *connection_player) const {
	assert(connection_);
	auto &connection = *connection_;

	connection.send(Message::S2C_State);
	//will patch message size in later, for now placeholder bytes:
	connection.send(uint8_t(0));
	connection.send(uint8_t(0));
	connection.send(uint8_t(0));
	connection.send(uint8_t(0));
	size_t mark = connection.send_buffer.size(); //keep track of this position in the buffer


	//send player info helper:
	auto send_player = [&](Player const &player) {
		connection.send(player.active);
	};

	send_player(player1);
	send_player(player2);

	//send transformation helper
	auto send_transformation = [&](Scene::Transform const &transform) {
		connection.send(transform.position);
		connection.send(transform.rotation);
		connection.send(transform.scale);
	};

	connection.send(static_cast<uint32_t>(scene.transforms.size()));
	for (auto it = scene.transforms.begin(); it != scene.transforms.end(); ++it) {
		send_transformation(*it);
	}

	//compute the message size and patch into the message header:
	uint32_t size = uint32_t(connection.send_buffer.size() - mark);
	connection.send_buffer[mark-4] = uint8_t(size);
	connection.send_buffer[mark-3] = uint8_t(size >> 8);
	connection.send_buffer[mark-2] = uint8_t(size >> 16);
	connection.send_buffer[mark-1] = uint8_t(size >> 24);
}

bool Game::recv_state_message(Connection *connection_) {
	assert(connection_);
	auto &connection = *connection_;
	auto &recv_buffer = connection.recv_buffer;

	if (recv_buffer.size() < 5) return false;
	if (recv_buffer[0] != uint8_t(Message::S2C_State)) return false;
	uint32_t size = (uint32_t(recv_buffer[4]) << 24)
				  | (uint32_t(recv_buffer[3]) << 16)
	              | (uint32_t(recv_buffer[2]) << 8)
	              |  uint32_t(recv_buffer[1]);
	//expecting complete message:
	if (recv_buffer.size() < 5 + size) return false;

	//copy bytes from buffer and advance position:
	uint32_t at = 0;
	auto read = [&](auto *val) {
		if (at + sizeof(*val) > size) {
			throw std::runtime_error("Ran out of bytes reading state message.");
		}
		std::memcpy(val, &recv_buffer[4 + at], sizeof(*val));
		at += sizeof(*val);
	};

	read(&player1.active);
	read(&player2.active);

	uint32_t transform_size;
	read(&transform_size);
	assert(static_cast<size_t>(transform_size) == scene.transforms.size());
	for (auto it = scene.transforms.begin(); it != scene.transforms.end(); ++it) {
		read(&(it->position));
		read(&(it->rotation));
		read(&(it->scale));
	}

	if (at != size) throw std::runtime_error("Trailing data in state message.");

	//delete message from buffer:
	recv_buffer.erase(recv_buffer.begin(), recv_buffer.begin() + 5 + size);

	return true;
}

void Game::draw(glm::uvec2 const &drawable_size) {
	//update camera aspect ratio for drawable:
	camera->aspect = float(drawable_size.x) / float(drawable_size.y);

	//set up light type and position for lit_color_texture_program:
	// TODO: consider using the Light(s) in the scene to do this
	glUseProgram(lit_color_texture_program->program);
	glUniform1i(lit_color_texture_program->LIGHT_TYPE_int, 1);
	glUniform3fv(lit_color_texture_program->LIGHT_DIRECTION_vec3, 1, glm::value_ptr(glm::vec3(0.0f, 0.0f,-1.0f)));
	glUniform3fv(lit_color_texture_program->LIGHT_ENERGY_vec3, 1, glm::value_ptr(glm::vec3(1.0f, 1.0f, 0.95f)));
	glUseProgram(0);

	glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
	glClearDepth(1.0f); //1.0 is actually the default value to clear the depth buffer to, but FYI you can change it.
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS); //this is the default depth comparison function, but FYI you can change it.

	scene.draw(*camera);

	GL_ERRORS();
}