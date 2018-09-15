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
	} controls;

	struct {
	    bool up = false;
	    bool down = false;
	    bool left = false;
	    bool right = false;
	    bool move_forward = false;
	    bool move_backward = false;
	    bool move_left = false;
	    bool move_right = false;
	} camera_controls;

	//looking down -y
	glm::quat const default_axis = glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
	float azimuth = 0;
	float elevation = 0;

	bool mouse_captured = false;

	Scene scene;
	Scene::Camera *camera = nullptr;

    std::map< int, Scene::Transform * > transform_dict;
    Scene::Object *player = nullptr;
    Scene::Object *walk = nullptr;

    WalkPoint wp;
    Scene::Transform *player_group;
    glm::vec3 player_normal = glm::vec3(0.0f, 0.0f, 1.0f);
    glm::quat player_twist = glm::quat(); //rotation on axis (0,0,1)

    glm::vec3 const player_up = glm::vec3(0.0f, 0.0f, 1.0f);
    glm::vec3 const player_forward = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 const player_right = glm::vec3(1.0f, 0.0f, 0.0f);

	//when this reaches zero, the 'dot' sample is triggered at the small crate:
	float dot_countdown = 1.0f;

	//this 'loop' sample is played at the large crate:
	std::shared_ptr< Sound::PlayingSample > loop;

//	WalkPoint wp;
};
