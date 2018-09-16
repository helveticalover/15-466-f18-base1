#include "Mode.hpp"

#include "WalkMesh.hpp"
#include "MeshBuffer.hpp"
#include "GL.hpp"
#include "Scene.hpp"
#include "Sound.hpp"

#include <SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <vector>
#include <random>
#include <cstddef>

static float const TIME_BETWEEN_RINGS_GLOBAL = 8.0f;
static float const TIME_BETWEEN_RINGS_PHONE = 25.0f;
static float const RING_DURATION = 15.0f;

static float const INTERACT_RADIUS = 2.5f;
static float const INTERACT_DOT = 0.8f;

// The 'CratesMode' shows scene with some crates in it:

struct CratesMode : public Mode {
	CratesMode();
	virtual ~CratesMode();

	//handle_event is called when new mouse or keyboard events are received:
	// (note that this might be many times per frame or never)
	//The function should return 'true' if it handled the event.
	virtual bool handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) override;

	//update is called at the start of a new frame, after events are handled:
	virtual void update(float elapsed) override;

	//draw is called after update:
	virtual void draw(glm::uvec2 const &drawable_size) override;

	//starts up a 'quit/resume' pause menu:
	void show_pause_menu();

	struct {
		bool forward = false;
		bool backward = false;
		bool left = false;
		bool right = false;
		bool try_interact = false;
		bool select = false;
	} controls;

	//looking down -y
	glm::quat const default_axis = glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
	float azimuth = 0;
	float elevation = 0;

	bool mouse_captured = false;

	Scene scene;
	Scene::Camera *camera = nullptr;

    std::map< int, Scene::Transform * > transform_dict;
    Scene::Object *player = nullptr;

    WalkPoint wp;
    Scene::Transform *player_group;
    glm::vec3 player_normal = glm::vec3(0.0f, 0.0f, 1.0f);
//    glm::vec3 player_forward = glm::vec3(0.0f, 1.0f, 0.0f);

    struct PhoneData {
        Scene::Object *phone_object;
        bool is_active = false;
        bool can_interact = false;
        float last_ring = -1.0f;
        float phone_delay = -1.0f;
        uint32_t identifier = 0;
    };

    PhoneData phone1;
    PhoneData phone2;
    PhoneData phone3;
    PhoneData phone4;
    std::vector< PhoneData * > const phone_list = {&phone1, &phone2, &phone3, &phone4};
    std::vector< PhoneData * > interact_list;

    struct {
        float time_since_last_ring = 2.0f;;
        PhoneData *last_phone = nullptr;
        PhoneData *next_phone = nullptr;
    } phone_state;

//    uint32_t max_active = 1;
//    uint32_t num_active = 0;
    float global_delay = TIME_BETWEEN_RINGS_GLOBAL;

    uint32_t strikes = 0;
    uint32_t merit = 0;

    PhoneData *phone_speaking_to = nullptr;
    bool speaking = false;
    bool mission = false;
    bool try_mission = false;

    uint32_t last_mission = 0;
    uint32_t codeword = -1;
    uint32_t selected_codeword = 0;
    std::vector< std::string > const codewords_list = {"HERON", "DOG", "FISH", "MONKEY", "BIRD"};

	//when this reaches zero, the 'dot' sample is triggered at the small crate:
	float dot_countdown = 1.0f;

	//this 'loop' sample is played at the large crate:
	std::shared_ptr< Sound::PlayingSample > loop;

	std::shared_ptr< Sound::PlayingSample > ringing1;
	std::shared_ptr< Sound::PlayingSample > ringing2;
	std::shared_ptr< Sound::PlayingSample > ringing3;
	std::shared_ptr< Sound::PlayingSample > ringing4;

	std::vector< std::shared_ptr< Sound::PlayingSample >> ringings = {ringing1, ringing2, ringing3, ringing4};
};
