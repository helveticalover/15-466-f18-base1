#include "CratesMode.hpp"

#include "MenuMode.hpp"
#include "Load.hpp"
#include "Sound.hpp"
#include "MeshBuffer.hpp"
#include "WalkMesh.hpp"
#include "gl_errors.hpp" //helper for dumpping OpenGL error messages
#include "read_chunk.hpp" //helper for reading a vector of structures from a file
#include "data_path.hpp" //helper to get paths relative to executable
#include "compile_program.hpp" //helper to compile opengl shader programs
#include "draw_text.hpp" //helper to... um.. draw text
#include "vertex_color_program.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <fstream>
#include <map>
#include <cstddef>
#include <random>
#include <algorithm>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

Load< WalkMesh > walk_mesh(LoadTagDefault, [](){
   	return new WalkMesh(data_path("phone-bank-walk.pnc"));
});

Load< MeshBuffer > crates_meshes(LoadTagDefault, [](){
	return new MeshBuffer(data_path("phone-bank.pnc"));
});

Load< GLuint > crates_meshes_for_vertex_color_program(LoadTagDefault, [](){
	return new GLuint(crates_meshes->make_vao_for_program(vertex_color_program->program));
});

Load< Sound::Sample > sample_dot(LoadTagDefault, [](){
	return new Sound::Sample(data_path("dot.wav"));
});
Load< Sound::Sample > sample_loop(LoadTagDefault, [](){
	return new Sound::Sample(data_path("loop.wav"));
});

CratesMode::CratesMode() {
	//----------------
	//set up scene:

	auto attach_object = [this](Scene::Transform *transform, std::string const &name) {
		Scene::Object *object = scene.new_object(transform);
		object->program = vertex_color_program->program;
		object->program_mvp_mat4 = vertex_color_program->object_to_clip_mat4;
		object->program_mv_mat4x3 = vertex_color_program->object_to_light_mat4x3;
		object->program_itmv_mat3 = vertex_color_program->normal_to_light_mat3;
		object->vao = *crates_meshes_for_vertex_color_program;
		MeshBuffer::Mesh const &mesh = crates_meshes->lookup(name);
		object->start = mesh.start;
		object->count = mesh.count;
		return object;
	};

    {
        std::ifstream file(data_path("phone-bank.scene"), std::ios::binary);

        std::vector< char > strings;
        read_chunk(file, "str0", &strings);

        struct TransformData {
            int parent_ref;
            uint32_t obj_name_begin, obj_name_end;
            float pos_x, pos_y, pos_z;
            float rot_x, rot_y, rot_z, rot_w;
            float scl_x, scl_y, scl_z;
        };

        std::vector< TransformData > transforms;
        read_chunk(file, "xfh0", &transforms);

        std::function< Scene::Transform *(int) > construct_transforms = [&](int ref) -> Scene::Transform *{
            if (transform_dict.find(ref) != transform_dict.end())
                return transform_dict[ref];

            TransformData *transform = (TransformData *)(&transforms[ref]);
            Scene::Transform *new_transform = scene.new_transform();
            new_transform->position = glm::vec3(transform->pos_x, transform->pos_y, transform->pos_z);
            new_transform->rotation = glm::quat(transform->rot_w, transform->rot_x, transform->rot_y, transform->rot_z);
            new_transform->scale = glm::vec3(transform->scl_x, transform->scl_y, transform->scl_z);
            new_transform->parent = transform->parent_ref < 0 ? nullptr : construct_transforms(transform->parent_ref);

//            if (!(transform->obj_name_begin <= transform->obj_name_end && transform->obj_name_end <= strings.size())) {
//                throw std::runtime_error("object scene entry has out-of-range name begin/end");
//            }

            transform_dict[ref] = new_transform;
            return new_transform;
        };

        struct MeshData {
        	int transform_ref;
        	uint32_t name_begin, name_end;
        };
        std::vector< MeshData > sceneObjects;
        read_chunk(file, "msh0", &sceneObjects);

        uint32_t num_phones = 0;
        for (auto const &entry : sceneObjects) {
            if (!(entry.name_begin <= entry.name_end && entry.name_end <= strings.size())) {
                throw std::runtime_error("mesh scene entry has out-of-range name begin/end");
            }
            std::string meshName(&strings[0] + entry.name_begin, &strings[0] + entry.name_end);

            if (!(entry.transform_ref >= 0 && entry.transform_ref <= (int)transforms.size())) {
                throw std::runtime_error("mesh scene entry has out of range transform ref");
            }

            if (meshName == "Phone_Flash") {
                continue;
			}

			Scene::Transform *transform_struct = construct_transforms(entry.transform_ref);
			Scene::Object *object = attach_object(transform_struct, meshName);

			if (meshName == "Phone") {
				PhoneData *phone = phone_list[num_phones];
				phone->phone_object = object;
				phone->identifier = num_phones;
				++num_phones;
			}

            if (meshName == "Player") {
                player = object;
                player_group = scene.new_transform();
                player_group->position = player->transform->position;
                player_group->rotation = player->transform->position;
                player_group->set_parent(player->transform->parent);

                player->transform->position = glm::vec3(0.0f, 0.0f, 0.0f);
                player->transform->rotation = glm::quat();
                player->transform->set_parent(player_group);

                wp = walk_mesh->start(player_group->position);
                player_group->position = walk_mesh->world_point(wp);
            }
        }
        assert(phone_list.size() == 4);
        phone_state.next_phone = phone_list[3];
        phone_state.last_phone = phone_list[3];
    }

	{
	    //Camera looking at the origin:
		Scene::Transform *transform = scene.new_transform();
		transform->position = glm::vec3(0.0f, 0.0f, 2.5f);
		//Cameras look along -z, so rotate view to look at origin:
		transform->rotation = default_axis;
		transform->set_parent(player_group);

		camera = scene.new_camera(transform);
	}

	//start the 'loop' sample playing at the large crate:
//	loop = sample_loop->play(large_crate->transform->position, 1.0f, Sound::Loop);
}

CratesMode::~CratesMode() {
	if (loop) loop->stop();
}

bool CratesMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {
	//ignore any keys that are the result of automatic key repeat:
	if (evt.type == SDL_KEYDOWN && evt.key.repeat) {
		return false;
	}
	//handle tracking the state of WSAD for movement control:
	if (evt.type == SDL_KEYDOWN || evt.type == SDL_KEYUP) {
		if (evt.key.keysym.scancode == SDL_SCANCODE_W) {
			controls.forward = (evt.type == SDL_KEYDOWN);
			return true;
		} else if (evt.key.keysym.scancode == SDL_SCANCODE_S) {
			controls.backward = (evt.type == SDL_KEYDOWN);
			return true;
		} else if (evt.key.keysym.scancode == SDL_SCANCODE_A) {
			controls.left = (evt.type == SDL_KEYDOWN);
			return true;
		} else if (evt.key.keysym.scancode == SDL_SCANCODE_D) {
			controls.right = (evt.type == SDL_KEYDOWN);
			return true;
		}
	}

	if (evt.type == SDL_KEYDOWN || evt.type == SDL_KEYUP) {
		if (evt.key.keysym.scancode == SDL_SCANCODE_LEFT) {
			camera_controls.move_left = (evt.type == SDL_KEYDOWN);
			return true;
		} else if (evt.key.keysym.scancode == SDL_SCANCODE_RIGHT) {
			camera_controls.move_right = (evt.type == SDL_KEYDOWN);
			return true;
		} else if (evt.key.keysym.scancode == SDL_SCANCODE_UP) {
			camera_controls.move_forward = (evt.type == SDL_KEYDOWN);
			return true;
		} else if (evt.key.keysym.scancode == SDL_SCANCODE_DOWN) {
            camera_controls.move_backward = (evt.type == SDL_KEYDOWN);
            return true;
        }
	}

	//handle tracking the mouse for rotation control:
	if (!mouse_captured) {
		if (evt.type == SDL_KEYDOWN && evt.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
			Mode::set_current(nullptr);
			return true;
		}
		if (evt.type == SDL_MOUSEBUTTONDOWN) {
			SDL_SetRelativeMouseMode(SDL_TRUE);
			mouse_captured = true;
			return true;
		}
	} else if (mouse_captured) {
		if (evt.type == SDL_KEYDOWN && evt.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
			SDL_SetRelativeMouseMode(SDL_FALSE);
			mouse_captured = false;
			return true;
		}
		if (evt.type == SDL_MOUSEMOTION) {
//			Note: float(window_size.y) * camera->fovy is a pixels-to-radians conversion factor
			float yaw = evt.motion.xrel / float(window_size.y) * camera->fovy;
			float pitch = evt.motion.yrel / float(window_size.y) * camera->fovy;
			yaw = -yaw;
			pitch = -pitch;
			azimuth += yaw;
			elevation += pitch;

			elevation = glm::clamp(elevation, glm::radians(-60.0f), glm::radians(60.0f));

			camera->transform->rotation = glm::normalize(
				default_axis
				* glm::angleAxis(azimuth, glm::vec3(0.0f, 1.0f, 0.0f))
				* glm::angleAxis(elevation, glm::vec3(1.0f, 0.0f, 0.0f))
			);
			player->transform->rotation = glm::normalize(glm::angleAxis(azimuth, glm::vec3(0.0f, 0.0f, 1.0f)));
			return true;
		}
	}
	return false;
}

void CratesMode::update(float elapsed) {

    auto ring_phone = [&](PhoneData *phone) {
        phone_state.next_phone->is_active = true;
        //	PLAY SOUND HERE
    };

    //CHECK FOR INTERACTION WITH PHONE

	phone_state.time_since_last_ring += elapsed;
	std::vector< PhoneData * > valid_phones;
	for (PhoneData *phone : phone_list) {

	    phone->last_ring += elapsed;

	    if (phone->is_active &&  phone->last_ring > RING_DURATION) {
            std::cout << "MISSED PHONE: " << std::to_string(phone->identifier) << std::endl;
	        phone->is_active = false;
	        ++strikes;
	    }

		if (phone->is_active) {
			MeshBuffer::Mesh const &mesh = crates_meshes->lookup("Phone_Flash");
			phone->phone_object->start = mesh.start;
			phone->phone_object->count = mesh.count;
		} else {
			MeshBuffer::Mesh const &mesh = crates_meshes->lookup("Phone");
			phone->phone_object->start = mesh.start;
			phone->phone_object->count = mesh.count;
		}

		if (phone->last_ring > TIME_BETWEEN_RINGS_PHONE || phone->last_ring == -1.0f) {
			valid_phones.emplace_back(phone);
		}
	}

	if (phone_state.next_phone == nullptr && valid_phones.size()) {
		phone_state.next_phone = valid_phones[rand()%valid_phones.size()];
	}

	if (phone_state.time_since_last_ring > TIME_BETWEEN_RINGS_GLOBAL && phone_state.next_phone != nullptr) {
		phone_state.next_phone->last_ring = 0.0f;
		phone_state.time_since_last_ring = 0.0f;
        phone_state.last_phone = phone_state.next_phone;

		ring_phone(phone_state.last_phone);

        std::cout << "RINGING PHONE: " << std::to_string(phone_state.last_phone->identifier) << std::endl;

        valid_phones.erase(std::remove(valid_phones.begin(), valid_phones.end(), phone_state.last_phone),
                valid_phones.end());

		if (!valid_phones.size()) {
			phone_state.next_phone = nullptr;
		} else {
			phone_state.next_phone = valid_phones[rand()%valid_phones.size()];
		}
	}

	//========================================================

    glm::mat3 directions = glm::mat3_cast(player_group->rotation * camera->transform->rotation);

	float amt = 5.0f * elapsed;
    glm::vec3 step = glm::vec3(0,0,0);
    if (controls.right) step += amt * directions[0];
	if (controls.left) step += -amt * directions[0];
	if (controls.backward) step += amt * directions[2];
	if (controls.forward) step += -amt * directions[2];

	if (step != glm::vec3(0,0,0)) {
        walk_mesh->walk(wp, step);
        player_group->position = walk_mesh->world_point(wp);
	}

//	if (camera_controls.move_right) camera->transform->position += amt * directions[0];
//	if (camera_controls.move_left) camera->transform->position += -amt * directions[0];
//	if (camera_controls.move_backward) camera->transform->position += amt * directions[2];
//	if (camera_controls.move_forward) camera->transform->position += -amt * directions[2];

	glm::vec3 target_normal = walk_mesh->world_normal(wp);

	if (target_normal != player_normal) {
        glm::vec3 from = player_normal;
        glm::vec3 to = target_normal;
        glm::vec3 axis = glm::normalize(glm::cross(from, to));
        float dot = glm::dot(from, to);
        glm::quat rotation = glm::angleAxis(glm::acos(dot), axis);

        if (std::isnan(rotation.x)) {
            return;
        }

        player_normal = target_normal;
		player_forward = glm::mat3_cast(rotation) * player_forward;

        player_group->rotation *= glm::normalize(rotation);
	}

	//https://stackoverflow.com/questions/1171849/finding-quaternion-representing-the-rotation-from-one-vector-to-another

//	{ //set sound positions:
//		glm::mat4 cam_to_world = camera->transform->make_local_to_world();
//		Sound::listener.set_position( cam_to_world[3] );
//		//camera looks down -z, so right is +x:
//		Sound::listener.set_right( glm::normalize(cam_to_world[0]) );
//
//		if (loop) {
//			glm::mat4 large_crate_to_world = large_crate->transform->make_local_to_world();
//			loop->set_position( large_crate_to_world * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f) );
//		}
//	}
//
//	dot_countdown -= elapsed;
//	if (dot_countdown <= 0.0f) {
//		dot_countdown = (rand() / float(RAND_MAX) * 2.0f) + 0.5f;
//		glm::mat4x3 small_crate_to_world = small_crate->transform->make_local_to_world();
//		sample_dot->play( small_crate_to_world * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f) );
//	}
}

void CratesMode::draw(glm::uvec2 const &drawable_size) {
	//set up basic OpenGL state:
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendEquation(GL_FUNC_ADD);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	//set up light position + color:
	glUseProgram(vertex_color_program->program);
	glUniform3fv(vertex_color_program->sun_color_vec3, 1, glm::value_ptr(glm::vec3(0.81f, 0.81f, 0.76f)));
	glUniform3fv(vertex_color_program->sun_direction_vec3, 1, glm::value_ptr(glm::normalize(glm::vec3(-0.2f, 0.2f, 1.0f))));
	glUniform3fv(vertex_color_program->sky_color_vec3, 1, glm::value_ptr(glm::vec3(0.4f, 0.4f, 0.45f)));
	glUniform3fv(vertex_color_program->sky_direction_vec3, 1, glm::value_ptr(glm::vec3(0.0f, 1.0f, 0.0f)));
	glUseProgram(0);

	//fix aspect ratio of camera
	camera->aspect = drawable_size.x / float(drawable_size.y);

	scene.draw(camera);

	if (Mode::current.get() == this) {
		glDisable(GL_DEPTH_TEST);
		std::string message;
		if (mouse_captured) {
			message = "ESCAPE TO UNGRAB MOUSE * WASD MOVE";
		} else {
			message = "CLICK TO GRAB MOUSE * ESCAPE QUIT";
		}
		float height = 0.06f;
		float width = text_width(message, height);
		draw_text(message, glm::vec2(-0.5f * width,-0.99f), height, glm::vec4(0.0f, 0.0f, 0.0f, 0.5f));
		draw_text(message, glm::vec2(-0.5f * width,-1.0f), height, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));

		glUseProgram(0);
	}

	GL_ERRORS();
}


void CratesMode::show_pause_menu() {
	std::shared_ptr< MenuMode > menu = std::make_shared< MenuMode >();

	std::shared_ptr< Mode > game = shared_from_this();
	menu->background = game;

	menu->choices.emplace_back("PAUSED");
	menu->choices.emplace_back("RESUME", [game](){
		Mode::set_current(game);
	});
	menu->choices.emplace_back("QUIT", [](){
		Mode::set_current(nullptr);
	});

	menu->selected = 1;

	Mode::set_current(menu);
}