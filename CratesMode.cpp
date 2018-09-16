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

static glm::vec3 project_on_plane(glm::vec3 x, glm::vec3 y, glm::vec3 z, glm::vec3 p);

Load< WalkMesh > walk_mesh(LoadTagDefault, [](){
   	return new WalkMesh(data_path("phone-bank-walk.pnc"));
});

Load< MeshBuffer > crates_meshes(LoadTagDefault, [](){
	return new MeshBuffer(data_path("phone-bank.pnc"));
});

Load< GLuint > crates_meshes_for_vertex_color_program(LoadTagDefault, [](){
	return new GLuint(crates_meshes->make_vao_for_program(vertex_color_program->program));
});

Load< Sound::Sample > ringtone1(LoadTagDefault, [](){
    return new Sound::Sample(data_path("sound/ring-001.wav"));
});

Load< Sound::Sample > ringtone2(LoadTagDefault, [](){
    return new Sound::Sample(data_path("sound/ring-002.wav"));
});

Load< Sound::Sample > ringtone3(LoadTagDefault, [](){
    return new Sound::Sample(data_path("sound/ring-003.wav"));
});

Load< Sound::Sample > ringtone4(LoadTagDefault, [](){
    return new Sound::Sample(data_path("sound/ring-004.wav"));
});

Load< Sound::Sample > sample_tone(LoadTagDefault, [](){
    return new Sound::Sample(data_path("sound/tone.wav"));
});

Load< Sound::Sample > sample_hangup(LoadTagDefault, [](){
    return new Sound::Sample(data_path("sound/hangup.wav"));
});

//Load< Sound::Sample > sample_dot(LoadTagDefault, [](){
//	return new Sound::Sample(data_path("dot.wav"));
//});
//Load< Sound::Sample > sample_loop(LoadTagDefault, [](){
//	return new Sound::Sample(data_path("loop.wav"));
//});

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

            if (!(transform->obj_name_begin <= transform->obj_name_end && transform->obj_name_end <= strings.size())) {
                throw std::runtime_error("object scene entry has out-of-range name begin/end");
            }

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

            if (meshName == "Phone_Flash" || meshName == "Phone_Interact") {
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
		transform->position = glm::vec3(0.0f, 0.0f, 2.0f);
		//Cameras look along -z, so rotate view to look at origin:
		transform->rotation = default_axis;
		transform->set_parent(player_group);

		camera = scene.new_camera(transform);
	}
}

CratesMode::~CratesMode() {
	if (loop) loop->stop();
	if (ringing1) ringing1->stop();
	if (ringing2) ringing2->stop();
	if (ringing3) ringing3->stop();
	if (ringing4) ringing4->stop();
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

    if (evt.type == SDL_KEYDOWN && evt.key.keysym.scancode == SDL_SCANCODE_SPACE) {
        speaking = false;
    }

	//	handle tracking the mouse for rotation control:
	if (!mouse_captured) {
		if (evt.type == SDL_KEYDOWN && evt.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
			Mode::set_current(nullptr);
			return true;
		}
		if (evt.type == SDL_MOUSEBUTTONDOWN && evt.button.button == SDL_BUTTON_RIGHT) {
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
//		Note: float(window_size.y) * camera->fovy is a pixels-to-radians conversion factor
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

	if ((evt.type == SDL_MOUSEBUTTONDOWN || evt.type == SDL_MOUSEBUTTONUP) && evt.button.button == SDL_BUTTON_LEFT) {
		controls.try_interact = evt.type == SDL_MOUSEBUTTONDOWN;
	}
	return false;
}

void CratesMode::update(float elapsed) {

    glm::mat3 directions = glm::mat3_cast(player_group->rotation * camera->transform->rotation);

    if (!speaking) {
        float amt = 7.5f * elapsed;
        glm::vec3 step = glm::vec3(0,0,0);
        if (controls.right) step += amt * directions[0];
        if (controls.left) step += -amt * directions[0];
        if (controls.backward) step += amt * directions[2];
        if (controls.forward) step += -amt * directions[2];

        if (step != glm::vec3(0,0,0)) {
            walk_mesh->walk(wp, step);
            player_group->position = walk_mesh->world_point(wp);
        }

        glm::vec3 target_normal = walk_mesh->world_normal(wp);

        if (target_normal != player_normal) {
            glm::vec3 from = player_normal;
            glm::vec3 to = target_normal;
            glm::vec3 axis = glm::normalize(glm::cross(from, to));
            float dot = glm::dot(from, to);

            if (dot == 1.0f || dot == -1.0f) {
                player_normal = target_normal;
                return;
            }

            glm::quat rotation = glm::angleAxis(glm::acos(dot), axis);

            if (std::isnan(std::acos(dot)) || std::isnan(rotation.x)) {
                std::cout << dot << std::endl;
                return;
            }

            player_normal = glm::mat3_cast(rotation) * player_normal;
            player_group->rotation *= glm::normalize(rotation);
        }
    }

    //==========================================================================

    glm::mat4 cam_to_world = camera->transform->make_local_to_world();
    Sound::listener.set_position( cam_to_world[3] );
    //camera looks down -z, so right is +x:
    Sound::listener.set_right( glm::normalize(cam_to_world[0]));

    // Helpers to handle phone behavior
    auto ring_phone = [&](PhoneData *phone) {
        std::cout << "RINGING PHONE: " << std::to_string(phone_state.last_phone->identifier) << std::endl;
        phone->is_active = true;
//        ++num_active;

        switch (phone->identifier) {
            case 0:
                ringing1 = ringtone1->play(phone_list[0]->phone_object->transform->position, 1.0f, Sound::Loop);
                ringing1->set_volume(10.0f);
                break;
            case 1:
                ringing2 = ringtone2->play(phone_list[1]->phone_object->transform->position, 1.0f, Sound::Loop);
                ringing2->set_volume(10.0f);
                break;
            case 2:
                ringing3 = ringtone3->play(phone_list[2]->phone_object->transform->position, 1.0f, Sound::Loop);
                ringing3->set_volume(10.0f);
                break;
            case 3:
                ringing4 = ringtone4->play(phone_list[3]->phone_object->transform->position, 1.0f, Sound::Loop);
                ringing4->set_volume(10.0f);
                break;
            default:
                break;
        }
    };

    auto stop_phone = [&](PhoneData *phone) {
        phone->is_active = false;
//        --num_active;

        switch (phone->identifier) {
            case 0:
                ringing1->stop();
                break;
            case 1:
                ringing2->stop();
                break;
            case 2:
                ringing3->stop();
                break;
            case 3:
                ringing4->stop();
                break;
            default:
                break;
        }
    };

    auto missed_phone = [&](PhoneData *phone) {
        stop_phone(phone);

        phone->phone_delay = TIME_BETWEEN_RINGS_PHONE;
        global_delay = TIME_BETWEEN_RINGS_GLOBAL;

        glm::mat4x3 phone_to_world = phone->phone_object->transform->make_local_to_world();
        sample_hangup->play( phone_to_world * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f), 2.0f, Sound::Once );
        ++strikes;
    };

    auto pickup_phone = [&](PhoneData *phone) {
        std::cout << "PICKUP PHONE" << std::endl;
        stop_phone(phone);

        speaking = true;
//        if (last_mission >= 2) {
//            float r = static_cast <float> (rand()) / static_cast <float> (RAND_MAX);
//            float denom = 6.0f - (float)(glm::min(last_mission, 5.0f));
//            mission = r < 1.0f / denom;
//        }

        phone->phone_delay = TIME_BETWEEN_RINGS_PHONE;
        global_delay = TIME_BETWEEN_RINGS_GLOBAL;

        glm::mat4x3 phone_to_world = phone->phone_object->transform->make_local_to_world();
        sample_hangup->play( phone_to_world * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f), 2.0f, Sound::Once );
        ++merit;
    };

	// Handle interaction
	auto can_interact = [&](PhoneData *phone) -> bool {
		glm::vec3 x = walk_mesh->vertices[wp.triangle.x];
		glm::vec3 y = walk_mesh->vertices[wp.triangle.y];
		glm::vec3 z = walk_mesh->vertices[wp.triangle.z];
		glm::vec3 player_to_phone = project_on_plane(x, y, z, phone->phone_object->transform->position)
									- player_group->position;
		float distance = glm::length(player_to_phone);
		glm::mat4 cam_to_world = camera->transform->make_local_to_world();

		glm::vec3 player_forward = project_on_plane(x, y, z, -cam_to_world[2]);
		player_forward = project_on_plane(x, y, z, player_forward);

		float dot = glm::dot(glm::normalize(player_to_phone), player_forward);
		return distance <= INTERACT_RADIUS && dot >= INTERACT_DOT;
	};

	interact_list.clear();
	for (PhoneData *phone : phone_list) {
		if (can_interact(phone)) {
            phone->can_interact = phone->is_active;
            if (phone->can_interact && controls.try_interact) {
                pickup_phone(phone);
            }
		} else {
		    phone->can_interact = false;
		}
	}

    // Phone ringing logic
	phone_state.time_since_last_ring += elapsed;
	std::vector< PhoneData * > valid_phones;
	for (PhoneData *phone : phone_list) {
	    phone->last_ring += elapsed;

	    if (phone->can_interact) {
			MeshBuffer::Mesh const &mesh = crates_meshes->lookup("Phone_Interact");
			phone->phone_object->start = mesh.start;
			phone->phone_object->count = mesh.count;
	    } else if (phone->is_active) {
			MeshBuffer::Mesh const &mesh = crates_meshes->lookup("Phone_Flash");
			phone->phone_object->start = mesh.start;
			phone->phone_object->count = mesh.count;
		} else {
			MeshBuffer::Mesh const &mesh = crates_meshes->lookup("Phone");
			phone->phone_object->start = mesh.start;
			phone->phone_object->count = mesh.count;
		}

        if (phone->is_active &&  phone->last_ring > RING_DURATION) {
            missed_phone(phone);
        }

		if (phone->last_ring > phone->phone_delay || phone->last_ring == -1.0f) {
			valid_phones.emplace_back(phone);
		}
	}

	if (phone_state.next_phone == nullptr && valid_phones.size()) {
		phone_state.next_phone = valid_phones[rand()%valid_phones.size()];
	}

	if (phone_state.time_since_last_ring > global_delay && phone_state.next_phone != nullptr
		/*&& num_active < max_active*/) {
		phone_state.next_phone->last_ring = 0.0f;
		phone_state.time_since_last_ring = 0.0f;
        phone_state.last_phone = phone_state.next_phone;

		ring_phone(phone_state.last_phone);

        valid_phones.erase(std::remove(valid_phones.begin(), valid_phones.end(), phone_state.last_phone),
                valid_phones.end());

		if (!valid_phones.size()) {
			phone_state.next_phone = nullptr;
		} else {
			phone_state.next_phone = valid_phones[rand()%valid_phones.size()];
		}
	}
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

		if (speaking) {
		    if (!mission) {
		        std::string speech = "JUST CHECKING IN";
				float height = 0.08f;
                GLint viewport[4];
                glGetIntegerv(GL_VIEWPORT, viewport);
                float aspect = viewport[2] / float(viewport[3]);
				draw_text(speech, glm::vec2(-aspect + 0.1f, 0.9f - height), height, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
		    }
			message = "SPACE TO CONTINUE";
		} else if (mouse_captured) {
			message = "WASD MOVE * LEFT CLICK INTERACT";
		} else {
			message = "RIGHT CLICK TO GRAB MOUSE * ESCAPE QUIT";
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

//Code from https://stackoverflow.com/questions/9605556/how-to-project-a-point-onto-a-plane-in-3d
static glm::vec3 project_on_plane(glm::vec3 x, glm::vec3 y, glm::vec3 z, glm::vec3 p) {
    glm::vec3 v0 = y - x;
    glm::vec3 v1 = z - x;
    glm::vec3 v2 = p - x;
    glm::vec3 n = glm::normalize(glm::cross(v0, v1));
    float dist = glm::dot(v2, n);
    return p - dist * n;
}