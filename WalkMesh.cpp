#include "WalkMesh.hpp"
#include "MeshBuffer.hpp"
#include "read_chunk.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

static glm::vec3 closest_point_on_triangle(glm::vec3 x, glm::vec3 y, glm::vec3 z, glm::vec3 p/*, bool &in_triangle*/);
static glm::vec3 project_point_on_plane(glm::vec3 x, glm::vec3 y, glm::vec3 z, glm::vec3 p);
static glm::vec3 world_to_barycentric(glm::vec3 x, glm::vec3 y, glm::vec3 z, glm::vec3 p);
static glm::vec3 barycentric_to_world(glm::vec3 x, glm::vec3 y, glm::vec3 z, glm::vec3 p);

WalkMesh::WalkMesh(std::string const &filename) {
    // TODO: Seems to be rotated 90 degrees around up axis
	std::ifstream file(filename, std::ios::binary);
	struct Vertex {
		glm::vec3 Position;
		glm::vec3 Normal;
		glm::u8vec4 Color;
	};
	static_assert(sizeof(Vertex) == 3*4+3*4+4*1, "Vertex is packed.");

	std::vector< Vertex > vertex_data;
	read_chunk(file, "pnc.", &vertex_data);

	std::vector< char > trash;
	read_chunk(file, "str0", &trash);
    read_chunk(file, "idx0", &trash);

	std::vector< uint32_t > triangle_data;
	read_chunk(file, "tri0", &triangle_data);

	if (file.peek() != EOF) {
		std::cerr << "WARNING: trailing data in mesh file '" << filename << "'" << std::endl;
	}

	std::vector< uint32_t > ref_to_index(vertex_data.size(), -1);

	for (uint32_t i = 0; i < vertex_data.size(); ++i) {
		Vertex data = vertex_data[i];
		glm::vec3 pos = data.Position;
		glm::vec3 norm = data.Normal;

		auto find_result = std::find(vertices.begin(), vertices.end(), pos);
		if (find_result == vertices.end()) {
			ref_to_index[i] = vertices.size();
			vertices.emplace_back(pos);
			vertex_normals.emplace_back(norm);
		} else {
			ref_to_index[i] = find_result - vertices.begin();
		}
	}

	for (auto const ref : triangle_data) {
        if (!(ref + 3 <= vertex_data.size())) {
            throw std::runtime_error("triangle reference out-of-range");
        }
        uint32_t a = ref_to_index[ref];
        uint32_t b = ref_to_index[ref + 1];
        uint32_t c = ref_to_index[ref + 2];

        if (a < 0 || b < 0 || c < 0) {
            throw std::runtime_error("ref not mapped to index");
        }

        next_vertex[glm::uvec2(a,b)] = c;
        next_vertex[glm::uvec2(b,c)] = a;
        next_vertex[glm::uvec2(c,a)] = b;

        triangles.emplace_back(glm::uvec3(a,b,c));
	}
}

WalkPoint WalkMesh::start(glm::vec3 const &world_point) const {
	WalkPoint walk_point;
	float distance = FLT_MAX;

	for (glm::uvec3 tri : triangles) {
        //https://www.gamedev.net/forums/topic/552906-closest-point-on-triangle/
        glm::vec3 x = vertices[tri.x];
        glm::vec3 y = vertices[tri.y];
        glm::vec3 z = vertices[tri.z];

        glm::vec3 closest = closest_point_on_triangle(x, y, z, world_point);
        float dist = glm::length(world_point - closest);
        if (dist < distance) {
            distance = dist;
            walk_point.triangle = tri;
            walk_point.weights = world_to_barycentric(x, y, z, closest);
//            std::cout << std::to_string(dist) << " " << glm::to_string(closest) << " " << glm::to_string(walk_point.weights) << std::endl;
        }
	}
    return walk_point;
}

void WalkMesh::walk(WalkPoint &wp, glm::vec3 const &step) const {
    glm::vec3 current_step = step;
    for (uint32_t i = 0; i < 2; ++i) {
        glm::vec3 world_new = current_step + world_point(wp);

        glm::vec3 x = vertices[wp.triangle.x];
        glm::vec3 y = vertices[wp.triangle.y];
        glm::vec3 z = vertices[wp.triangle.z];

        glm::vec3 project_on_plane = project_point_on_plane(x, y, z, world_new);

        glm::vec3 project_on_triangle = closest_point_on_triangle(x, y, z, world_new);
        glm::vec3 new_weights = world_to_barycentric(x, y, z, project_on_triangle);

        glm::uvec2 edge = glm::uvec2(0,0);  //offset
        // cross triangle edge -- push point against edge or vertex

        glm::vec3 prev_weights = new_weights;
        if (new_weights.x <= 0) {
            float scale = 1.0f - new_weights.x;
            new_weights = glm::vec3(0.0f, new_weights.y / scale, new_weights.z / scale);
            edge = glm::uvec2(1,2);
        }
        if (new_weights.y <= 0) {
            float scale = 1.0f - new_weights.y;
            new_weights = glm::vec3(new_weights.x / scale, 0.0f, new_weights.z / scale);
            edge = glm::uvec2(2,0);
        }
        if (new_weights.z <= 0) {
            float scale = 1.0f - new_weights.z;
            new_weights = glm::vec3(new_weights.x / scale, new_weights.y / scale, 0);
            edge = glm::uvec2(0,1);
        }

        if (new_weights.x < 0 || new_weights.y < 0 || new_weights.z < 0) {
            std::cerr << "Weights have negative values: " + glm::to_string(new_weights) << std::endl;
        }

        // on vertex -- has two possible edges
        glm::uvec2 alt_edge = glm::uvec2(0,0);
        if (new_weights.x == 1) {
            edge = glm::uvec2(0,1);
            alt_edge = glm::uvec2(2,0);
        } else if (new_weights.y == 1) {
            edge = glm::uvec2(1,2);
            alt_edge = glm::uvec2(0,1);
        } else if (new_weights.z == 1) {
            edge = glm::uvec2(2,0);
            alt_edge = glm::uvec2(1,2);
        }

        // break out of loop if step finished
        wp.weights = new_weights;
        current_step = project_on_plane - barycentric_to_world(x, y, z, new_weights);
        if (glm::length(current_step) < 0.001) {
//            std::cout << "finished step" << std::endl;
            break;
        }

        if (edge == glm::uvec2(0,0)) {
//            std::cerr << "On edge / step finished mismatch: " + glm::to_string(wp.weights) + " " + glm::to_string(prev_weights) << std::endl;
            break;
        }

        // break out of loop if no adjacent triangles
        glm::uvec2 use_edge = edge;
        auto new_vertex = next_vertex.find(glm::uvec2(*(&wp.triangle.x + edge.y), *(&wp.triangle.x + edge.x)));
        auto this_vertex = next_vertex.find(glm::uvec2(*(&wp.triangle.x + edge.x), *(&wp.triangle.x + edge.y)));
        if (this_vertex == next_vertex.end()) {
//            throw std::runtime_error("Couldn't find this vertex: " + std::to_string(*(&wp.triangle.x + edge.x)) + " " + std::to_string(*(&wp.triangle.x + edge.y)) + " " + glm::to_string(edge));
        }
        if (new_vertex == next_vertex.end() || new_vertex->second == this_vertex->second) {
            if (alt_edge == glm::uvec2(0,0)) {
//                std::cout << "no more triangles: " + glm::to_string(glm::uvec2(*(&wp.triangle.x + edge.x), *(&wp.triangle.x + edge.y))) << std::endl;
                break;
            }

//            std::cout << "on vertex " + glm::to_string(wp.weights) << std::endl;
            new_vertex = next_vertex.find(glm::uvec2(*(&wp.triangle.x + alt_edge.y), *(&wp.triangle.x + alt_edge.x)));
            if (new_vertex == next_vertex.end() || new_vertex->second == this_vertex->second) {
//                std::cout << "no more triangles: " + glm::to_string(wp.triangle) << std::endl;
                break;
            }
            use_edge = alt_edge;
        }

        // NOTE: don't use meshes with > 90 degree inclines
        // break out of loop if next triangle is too steep
        WalkPoint new_wp;
        auto try_walk = [&](glm::uvec2 use_edge){
            new_wp.triangle = glm::uvec3(*(&wp.triangle.x + use_edge.y), *(&wp.triangle.x + use_edge.x), new_vertex->second);
            new_wp.weights = glm::vec3(*(&wp.weights.x + use_edge.y), *(&wp.weights.x + use_edge.x), 0.0f);
            return glm::dot(world_normal(wp), world_normal(new_wp)) > 0;
        };

        if (!try_walk(use_edge)) {
//            std::cout << "can't walk on 1st edge " + glm::to_string(glm::uvec2(*(&wp.triangle.x + use_edge.x), *(&wp.triangle.x + use_edge.y))) + " " + glm::to_string(new_wp.triangle) << std::endl;
//            if (use_edge == alt_edge || alt_edge == glm::uvec2(0,0)) {
//                break;
//            }
//            new_vertex = next_vertex.find(glm::uvec2(*(&wp.triangle.x + alt_edge.y), *(&wp.triangle.x + alt_edge.x)));
//            if (new_vertex == next_vertex.end() || new_vertex->second == this_vertex->second) {
//                break;
//            }
//            if (!try_walk(alt_edge)) {
//                std::cout << "can't walk on 2nd edge " + glm::to_string(glm::uvec2(*(&wp.triangle.x + alt_edge.x), *(&wp.triangle.x + alt_edge.y))) + " " + glm::to_string(new_wp.triangle) << std::endl;
//                break;
//            }
            break;
        }

//        std::cout << "new triangle" << " " << std::to_string(*(&wp.triangle.x + edge.y)) << " " << std::to_string(*(&wp.triangle.x + edge.x)) << " " << glm::to_string(new_wp.triangle) << " " << glm::to_string(wp.triangle) << std::endl;
        wp = new_wp;
    }
}

//Cdoe from https://www.gamedev.net/forums/topic/552906-closest-point-on-triangle/
static glm::vec3 closest_point_on_triangle(glm::vec3 x, glm::vec3 y, glm::vec3 z, glm::vec3 p/*, bool &in_triangle*/) {
    glm::vec3 edge0 = y - x;
    glm::vec3 edge1 = z - x;
    glm::vec3 v0 = x - p;

    float a = glm::dot(edge0, edge0);
    float b = glm::dot(edge0, edge1);
    float c = glm::dot(edge1, edge1);
    float d = glm::dot(edge0, v0);
    float e = glm::dot(edge1, v0 );

    float det = a*c - b*b;
    float s = b*e - c*d;
    float t = b*d - a*e;

    if ( s + t < det ) {
        if ( s < 0.f ) {
            if ( t < 0.f ) {
                if ( d < 0.f ) {
                    s = glm::clamp( -d/a, 0.f, 1.f );
                    t = 0.f;
                } else {
                    s = 0.f;
                    t = glm::clamp( -e/c, 0.f, 1.f );
                }
            } else {
                s = 0.f;
                t = glm::clamp( -e/c, 0.f, 1.f );
            }
        } else if ( t < 0.f ) {
            s = glm::clamp( -d/a, 0.f, 1.f );
            t = 0.f;
        } else {
            float invDet = 1.f / det;
            s *= invDet;
            t *= invDet;
        }
    } else {
        if ( s < 0.f ) {
            float tmp0 = b+d;
            float tmp1 = c+e;
            if ( tmp1 > tmp0 ) {
                float numer = tmp1 - tmp0;
                float denom = a-2*b+c;
                s = glm::clamp( numer/denom, 0.f, 1.f );
                t = 1-s;
            } else {
                t = glm::clamp( -e/c, 0.f, 1.f );
                s = 0.f;
            }
        } else if ( t < 0.f ) {
            if ( a+d > b+e ) {
                float numer = c+e-b-d;
                float denom = a-2*b+c;
                s = glm::clamp( numer/denom, 0.f, 1.f );
                t = 1-s;
            } else {
                s = glm::clamp( -e/c, 0.f, 1.f );
                t = 0.f;
            }
        } else {
            float numer = c+e-b-d;
            float denom = a-2*b+c;
            s = glm::clamp( numer/denom, 0.f, 1.f );
            t = 1.f - s;
        }
    }
    return x + s * edge0 + t * edge1;
}

//Code from https://stackoverflow.com/questions/9605556/how-to-project-a-point-onto-a-plane-in-3d
static glm::vec3 project_point_on_plane(glm::vec3 x, glm::vec3 y, glm::vec3 z, glm::vec3 p) {
    glm::vec3 v0 = y - x;
    glm::vec3 v1 = z - x;
    glm::vec3 v2 = p - x;
    glm::vec3 n = glm::normalize(glm::cross(v0, v1));
    float dist = glm::dot(v2, n);
    return p - dist * n;
}

//Code from https://gamedev.stackexchange.com/questions/23743/whats-the-most-efficient-way-to-find-barycentric-coordinates
static glm::vec3 world_to_barycentric(glm::vec3 x, glm::vec3 y, glm::vec3 z, glm::vec3 p) {
    glm::vec3 edge0 = y - x;
    glm::vec3 edge1 = z - x;
    glm::vec3 v0 = x - p;
    float a = glm::dot(edge0, edge0);
    float b = glm::dot(edge0, edge1);
    float c = glm::dot(edge1, edge1);
    float d = glm::dot(v0, edge0);
    float e = glm::dot(v0, edge1);
    float invDenom = 1.0 / (a*c - b*b);
    float s = b*e - c*d;
    float t = b*d - a*e;
    float v = s * invDenom;
    float w = t * invDenom;
    float u = 1.0f - v - w;
    return glm::vec3(u,v,w);
}

static glm::vec3 barycentric_to_world(glm::vec3 x, glm::vec3 y, glm::vec3 z, glm::vec3 p) {
    return p.x * x + p.y * y + p.z * z;
}