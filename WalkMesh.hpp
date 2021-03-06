#pragma once

#include "GL.hpp"
#include "MeshBuffer.hpp"

#include <vector>
#include <unordered_map>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp> //allows the use of 'uvec2' as an unordered_map key

struct WalkPoint {
    glm::uvec3 triangle = glm::uvec3(-1U); //indices of current triangle
    glm::vec3 weights = glm::vec3(std::numeric_limits< float >::quiet_NaN()); //barycentric coordinates for current point
};

struct WalkMesh {

	//Construct new WalkMesh and build next_vertex structure:
	WalkMesh(std::string const &filename);

    //Walk mesh will keep track of triangles, vertices:
    std::vector< glm::vec3 > vertices;
    std::vector< glm::uvec3 > triangles; //CCW-oriented
    std::vector< glm::vec3 > vertex_normals;

    //This "next vertex" map includes [a,b]->c, [b,c]->a, and [c,a]->b for each triangle, and is useful for checking what's over an edge from a given point:
    std::unordered_map< glm::uvec2, uint32_t > next_vertex;

	//used to initialize walking -- finds the closest point on the walk mesh:
	// (should only need to call this at the start of a level)
	WalkPoint start(glm::vec3 const &world_point) const;

	//used to update walk point:
	void walk(WalkPoint &wp, glm::vec3 const &step) const;

	//used to read back results of walking:
	glm::vec3 world_point(WalkPoint &walk_point) const {
		return walk_point.weights.x * vertices[walk_point.triangle.x]
		     + walk_point.weights.y * vertices[walk_point.triangle.y]
		     + walk_point.weights.z * vertices[walk_point.triangle.z];
	}

	glm::vec3 world_normal(WalkPoint &walk_point) const {
		return glm::normalize(
			vertex_normals[walk_point.triangle.x] * walk_point.weights.x +
			vertex_normals[walk_point.triangle.y] * walk_point.weights.y +
			vertex_normals[walk_point.triangle.z] * walk_point.weights.z);
	}
};

/*
// The intent is that game code will work something like this:

Load< WalkMesh > walk_mesh;

Game {
	WalkPoint walk_point;
}
Game::Game() {
	//...
	walk_point = walk_mesh->start(level_start_position);
}

Game::update(float elapsed) {
	//update position on walk mesh:
	glm::vec3 step = player_forward * speed * elapsed;
	walk_mesh->walk(walk_point, step);

	//update player position:
	player_at = walk_mesh->world_point(walk_point);

	//update player orientation:
	glm::vec3 old_player_up = player_up;
	player_up = walk_mesh->world_normal(walk_point);

	glm::quat orientation_change = (compute rotation that takes old_player_up to player_up)
	player_forward = orientation_change * player_forward;

	//make sure player_forward is perpendicular to player_up (the earlier rotation should ensure that, but it might drift over time):
	player_forward = glm::normalize(player_forward - player_up * glm::dot(player_up, player_forward));

	//compute rightward direction from forward and up:
	player_right = glm::cross(player_forward, player_up);

}
*/