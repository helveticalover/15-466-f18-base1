#include "WalkMesh.hpp"
#include "MeshBuffer.hpp"
#include "read_chunk.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

#define VERTEX_OFFSET 0.001f

static glm::vec3 closest_point_on_triangle(glm::vec3 x, glm::vec3 y, glm::vec3 z, glm::vec3 p);
static glm::vec3 world_to_barycentric(glm::vec3 x, glm::vec3 y, glm::vec3 z, glm::vec3 p);
static glm::vec3 barycentric_to_world(glm::vec3 x, glm::vec3 y, glm::vec3 z, glm::vec3 p);

WalkMesh::WalkMesh(std::string const &filename) {
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
        }
	}
    return walk_point;
}

void WalkMesh::walk(WalkPoint &wp, glm::vec3 const &step) const {
    glm::vec3 current_step = step;
    float distance_to_travel = 1.0f;

    uint32_t count = 0;
    while (distance_to_travel > 0.0f) {

        if (count > 5) break;

        glm::vec3 x = vertices[wp.triangle.x];
        glm::vec3 y = vertices[wp.triangle.y];
        glm::vec3 z = vertices[wp.triangle.z];

        // Offset vertices to adjacent edge
        if (wp.weights.x == 1.0f) {
            wp.weights.x -= VERTEX_OFFSET;
            wp.weights.y += VERTEX_OFFSET;
        }
        if (wp.weights.y == 1.0f) {
            wp.weights.y -= VERTEX_OFFSET;
            wp.weights.z += VERTEX_OFFSET;
        }
        if (wp.weights.z == 1.0f) {
            wp.weights.z -= VERTEX_OFFSET;
            wp.weights.x += VERTEX_OFFSET;
        }

        glm::vec3 bary_step_end = world_to_barycentric(x, y, z, world_point(wp) + current_step);
        glm::vec3 bary_step = bary_step_end - wp.weights;

        float tx = bary_step.x != 0.0f ? -wp.weights.x / bary_step.x : FLT_MAX;
        float ty = bary_step.y != 0.0f ? -wp.weights.y / bary_step.y : FLT_MAX;
        float tz = bary_step.z != 0.0f ? -wp.weights.z / bary_step.z : FLT_MAX;

        tx = tx <= 0.0f ? FLT_MAX : tx;
        ty = ty <= 0.0f ? FLT_MAX : ty;
        tz = tz <= 0.0f ? FLT_MAX : tz;

        float t_final = glm::min(tx, glm::min(ty, tz));
        t_final = glm::min(1.0f, t_final);

        // Don't towards edge if already on that edge
        if ((wp.weights.x == 0.0f && bary_step.x < 0.0f) ||
            (wp.weights.y == 0.0f && bary_step.y < 0.0f) ||
            (wp.weights.z == 0.0f && bary_step.z < 0.0f)) {
            t_final = 0.0f;
        }

        // If step is in triangle's normal
        if (t_final == FLT_MAX) {
            break;
        }

        glm::vec3 bary_end = wp.weights + t_final * bary_step;
        distance_to_travel = (1.0f - t_final) * distance_to_travel;

        if (bary_end.x < 0.0f || bary_end.y < 0.0f || bary_end.z < 0.0f) {
            break;
        }

        if (distance_to_travel == 0.0f) {
            wp.weights = bary_end;
        } else {
            // Find next triangle if against edge and still have distance to travel
            std::vector< glm::uvec2 > valid_edges;
            std::vector< glm::uvec2 > offset_index;
            glm::uvec3 new_triangle = wp.triangle;
            glm::uvec2 final_offset = glm::uvec2(0,0);

            if (bary_end.x  <= 0.0f) {
                valid_edges.emplace_back(glm::uvec2(wp.triangle.z, wp.triangle.y));
                offset_index.emplace_back(glm::uvec2(2,1));
            }
            if (bary_end.y <= 0.0f) {
                valid_edges.emplace_back(glm::uvec2(wp.triangle.x, wp.triangle.z));
                offset_index.emplace_back(glm::uvec2(0,2));
            }
            if (bary_end.z <= 0.0f) {
                valid_edges.emplace_back(glm::uvec2(wp.triangle.y, wp.triangle.x));
                offset_index.emplace_back(glm::uvec2(1,0));
            }

            assert(valid_edges.size() <= 2);

            for (uint32_t i = 0; i < valid_edges.size(); ++i) {
                auto next_vert = next_vertex.find(valid_edges[i]);
                if (next_vert != next_vertex.end()) {
                    final_offset = offset_index[i];
                    new_triangle = glm::uvec3(valid_edges[i].x, valid_edges[i].y, next_vert->second);
                    break;
                }
            }

            if (new_triangle == wp.triangle) {
                // Slide along edge components

                if (bary_end.x >= 1.0f || bary_end.y >= 1.0f || bary_end.z >= 1.0f || !valid_edges.size()) {
                    wp.weights = bary_end;
                    break;
                }

                glm::vec3 v1 = vertices[*(&wp.triangle.x + offset_index[0].y)];
                glm::vec3 v2 = vertices[*(&wp.triangle.x + offset_index[0].x)];

                glm::vec3 new_step = barycentric_to_world(x, y, z, bary_step_end - bary_end);
                glm::vec3 from = glm::normalize(new_step);
                glm::vec3 to = glm::normalize(v2 - v1);
                float dot = glm::dot(from, to);
                float angle = dot < 0.0f ? glm::acos(dot) - M_PI : glm::acos(dot);
                glm::vec3 axis = glm::normalize(glm::cross(from, to));
                glm::quat rotate = glm::angleAxis(angle, axis);

                if (std::isnan(rotate.x)) {
                    wp.weights = bary_end;
                    break;
                }

                current_step = glm::mat3_cast(rotate) * new_step;

            } else {
                // Go to next triangle
                glm::vec3 new_weights = glm::vec3(0.0f, 0.0f, 0.0f);
                new_weights.x = *(&bary_end.x + final_offset.x);
                new_weights.y = *(&bary_end.x + final_offset.y);

                wp.weights = new_weights;
                wp.triangle = new_triangle;

                glm::vec3 new_step = barycentric_to_world(x, y, z, bary_step_end - bary_end);
                glm::vec3 from = glm::normalize(step);
                glm::vec3 to = glm::normalize(new_step);
                glm::vec3 axis = glm::normalize(glm::cross(from, to));
                float dot = glm::dot(from, to);
                glm::quat rotate = glm::angleAxis(glm::acos(dot), axis);

                if (std::isnan(rotate.x)) {
                    current_step = new_step;
                } else {
                    current_step = glm::mat3_cast(rotate) * new_step;
                }

            }
        }


        ++count;
    }
}

//Cdoe from https://www.gamedev.net/forums/topic/552906-closest-point-on-triangle/
static glm::vec3 closest_point_on_triangle(glm::vec3 x, glm::vec3 y, glm::vec3 z, glm::vec3 p) {
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