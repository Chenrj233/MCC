#include <glad\glad.h>
#include <glfw/glfw3.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb\stb_image.h>

#include <camera.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/noise.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <time.h>
#include <generator.h>
#include <light.h>

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void processInput(GLFWwindow *window);

ChunkGeneratorOverWorldGrain *c = new ChunkGeneratorOverWorldGrain();
// settings
const unsigned int SCR_WIDTH = 800;
const unsigned int SCR_HEIGHT = 600;

// camera
Camera camera(glm::vec3(0.0f, 0.0f, 3.0f));
float lastX = SCR_WIDTH / 2.0f;
float lastY = SCR_HEIGHT / 2.0f;
bool firstMouse = true;

// timing
float deltaTime = 0.0f;	// time between current frame and last frame
float lastFrame = 0.0f;

static GLuint program;
static GLuint depthProgram;
static GLuint depthTestProgram;
const static GLint attribute_coord = 0;
const static GLint normal_coord = 1;
const static GLint tangent_coord = 2;
const static GLint bitangent_coord = 3;
static GLint uniform_mvp;
static GLuint texture, normalMap;
static GLint uniform_texture, uniform_normalMap;
static GLuint cursor_vbo;
static GLint uniform_viewpos;
static DirlightUniform uniform_dirlight;
static GLint uniform_lightspacematrix_normal;
static GLint uniform_lightspacematrix_depth;
static GLint uniform_shadow;

static DirectLight dirlight;

static glm::vec3 position;
static glm::vec3 fforward;
static glm::vec3 rright;
static glm::vec3 up;
static glm::vec3 lookat;
static glm::vec3 angle;

static int ww, wh;
static int mx, my, mz;
static int face;
static uint8_t buildtype = 1;

static time_t now;
static unsigned int keys;
static bool select_using_depthbuffer = true;

// Size of one chunk in blocks
#define CX 16
#define CY 16
#define CZ 16

// Number of chunks in the world
#define SCX 32
#define SCY 16
#define SCZ 32

int blocks[CX*SCX][CY*SCY][CZ*SCZ];

// Sea level
#define SEALEVEL 4

// Number of VBO slots for chunks
#define CHUNKSLOTS (SCX * SCY * SCZ)

static const int transparent[16] = { 2, 0, 0, 0, 1, 0, 0, 0, 3, 4, 0, 0, 0, 0, 0, 0 };
static const char *blocknames[16] = {
	"air", "dirt", "topsoil", "grass", "leaves", "wood", "stone", "sand",
	"water", "glass", "brick", "ore", "woodrings", "white", "black", "x-y"
};

struct byte4 {
	uint8_t x, y, z, w;
	byte4() {}
	byte4(const uint8_t &x, const uint8_t &y, const uint8_t &z, const uint8_t &w) : x(x), y(y), z(z), w(w) {}
};

static struct chunk *chunk_slot[CHUNKSLOTS] = { 0 };

struct chunk {
	int blk[CX][CY][CZ];
	struct chunk *left, *right, *below, *above, *front, *back;
	int slot;
	GLuint vbo, vbo_normal,vbo_tangent,vbo_bitangent;
	int elements;
	time_t lastused;
	bool changed;
	bool noised;
	bool initialized;
	int ax;
	int ay;
	int az;

	chunk() : ax(0), ay(0), az(0) {
		memset(blk, 0, sizeof blk);
		left = right = below = above = front = back = 0;
		lastused = now;
		slot = 0;
		changed = true;
		initialized = false;
		noised = false;
	}

	chunk(int x, int y, int z) : ax(x), ay(y), az(z) {
		memset(blk, 0, sizeof blk);
		left = right = below = above = front = back = 0;
		lastused = now;
		slot = 0;
		changed = true;
		initialized = false;
		noised = false;
	}

	uint8_t get(int x, int y, int z) const {
		if (x < 0)
			return left ? left->blk[x + CX][y][z] : 0;
		if (x >= CX)
			return right ? right->blk[x - CX][y][z] : 0;
		if (y < 0)
			return below ? below->blk[x][y + CY][z] : 0;
		if (y >= CY)
			return above ? above->blk[x][y - CY][z] : 0;
		if (z < 0)
			return front ? front->blk[x][y][z + CZ] : 0;
		if (z >= CZ)
			return back ? back->blk[x][y][z - CZ] : 0;
		return blk[x][y][z];
	}

	bool isblocked(int x1, int y1, int z1, int x2, int y2, int z2) {
		// Invisible blocks are always "blocked"
		if (!blk[x1][y1][z1])
			return true;

		// Leaves do not block any other block, including themselves
		if (transparent[get(x2, y2, z2)] == 1)
			return false;

		// Non-transparent blocks always block line of sight
		if (!transparent[get(x2, y2, z2)])
			return true;

		// Otherwise, LOS is only blocked by blocks if the same transparency type
		if (x1 > 16 || x2 > 16 || y1 > 16 || y2 > 16 || z1 > 16 || z2 > 16)
			return true;
		return transparent[get(x2, y2, z2)] == transparent[blk[x1][y1][z1]];
	}

	void set(int x, int y, int z, uint8_t type) {
		// If coordinates are outside this chunk, find the right one.
		if (x < 0) {
			if (left)
				left->set(x + CX, y, z, type);
			return;
		}
		if (x >= CX) {
			if (right)
				right->set(x - CX, y, z, type);
			return;
		}
		if (y < 0) {
			if (below)
				below->set(x, y + CY, z, type);
			return;
		}
		if (y >= CY) {
			if (above)
				above->set(x, y - CY, z, type);
			return;
		}
		if (z < 0) {
			if (front)
				front->set(x, y, z + CZ, type);
			return;
		}
		if (z >= CZ) {
			if (back)
				back->set(x, y, z - CZ, type);
			return;
		}

		// Change the block
		blk[x][y][z] = type;
		changed = true;

		// When updating blocks at the edge of this chunk,
		// visibility of blocks in the neighbouring chunk might change.
		if (x == 0 && left)
			left->changed = true;
		if (x == CX - 1 && right)
			right->changed = true;
		if (y == 0 && below)
			below->changed = true;
		if (y == CY - 1 && above)
			above->changed = true;
		if (z == 0 && front)
			front->changed = true;
		if (z == CZ - 1 && back)
			back->changed = true;
	}

	static float noise2d(float x, float y, int seed, int octaves, float persistence) {
		float sum = 0;
		float strength = 1.0;
		float scale = 1.0;

		for (int i = 0; i < octaves; i++) {
			sum += strength * glm::simplex(glm::vec2(x, y) * scale);
			scale *= 2.0;
			strength *= persistence;
		}

		return sum;
	}

	static float noise3d_abs(float x, float y, float z, int seed, int octaves, float persistence) {
		float sum = 0;
		float strength = 1.0;
		float scale = 1.0;

		for (int i = 0; i < octaves; i++) {
			sum += strength * fabs(glm::simplex(glm::vec3(x, y, z) * scale));
			scale *= 2.0;
			strength *= persistence;
		}

		return sum;
	}

	void noise()
	{
		if (noised)
			return;
		else
			noised = true;
		//ChunkColumnStorage chunk1 = c->Generate(ax, ay, GeneratorSettings());
		for (int x = 0; x < CX; x++)
		{
			for (int y = 0; y < CY; y++)
			{
				for (int z = 0; z < CZ; z++)
				{
					blk[x][y][z] = blocks[(ax + SCX / 2)*CX + x][(ay + SCY / 2)*CY + y][(az + SCZ / 2)*CZ + z];
				}
			}
		}
		//delete c;
		changed = true;
	}

	void noise(int seed) {
		if (noised)
			return;
		else
			noised = true;

		for (int x = 0; x < CX; x++) {
			for (int z = 0; z < CZ; z++) {
				// Land height
				float n = noise2d((x + ax * CX) / 256.0, (z + az * CZ) / 256.0, seed, 5, 0.8) * 4;
				int h = n * 2;
				int y = 0;

				// Land blocks
				for (y = 0; y < CY; y++) {
					// Are we above "ground" level?
					if (y + ay * CY >= h) {
						// If we are not yet up to sea level, fill with water blocks
						if (y + ay * CY < SEALEVEL) {
							blk[x][y][z] = 8;
							continue;
							// Otherwise, we are in the air
						}
						else
							break;
					}

					// Random value used to determine land type
					float r = noise3d_abs((x + ax * CX) / 16.0, (y + ay * CY) / 16.0, (z + az * CZ) / 16.0, -seed, 2, 1);

					// Sand layer
					if (n + r * 5 < 4)
						blk[x][y][z] = 7;
					// Dirt layer, but use grass blocks for the top
					else if (n + r * 5 < 8)
						blk[x][y][z] = (h < SEALEVEL || y + ay * CY < h - 1) ? 1 : 3;
					// Rock layer
					else if (r < 1.25)
						blk[x][y][z] = 6;
					// Sometimes, ores!
					else
						blk[x][y][z] = 11;
				}
			}
		}
		changed = true;
	}

	void update() {
		byte4 vertex[CX * CY * CZ * 18];
		//1 represent (1, 0, 0), 2 represent (0, 1, 0), 3 represent (0, 0, 1)
		//-1 represent (-1, 0, 0), -2 represent (0, -1, 0), -3 represent (0, 0, -1)
		GLfloat normal[CX * CY * CZ * 18];
		glm::vec3 tangent[CX * CY * CZ * 18];
		glm::vec3 bitangent[CX * CY * CZ * 18];
		int i = 0;
		int merged = 0;
		bool vis = false;

		// View from negative x

		for (int x = CX - 1; x >= 0; x--) {
			for (int y = 0; y < CY; y++) {
				for (int z = 0; z < CZ; z++) {
					// Line of sight blocked?
					if (isblocked(x, y, z, x - 1, y, z)) {
						vis = false;
						continue;
					}

					uint8_t top = blk[x][y][z];
					uint8_t bottom = blk[x][y][z];
					uint8_t side = blk[x][y][z];

					// Grass block has dirt sides and bottom
					if (top == 2) {
						bottom = 3;
						side = 9;
						// Wood blocks have rings on top and bottom
					}
					else if (top == 5) {
						top = bottom = 12;
					}

					// Same block as previous one? Extend it.
					if (vis && z != 0 && blk[x][y][z] == blk[x][y][z - 1]) {
						vertex[i - 5] = byte4(x, y, z + 1, side);
						vertex[i - 2] = byte4(x, y, z + 1, side);
						vertex[i - 1] = byte4(x, y + 1, z + 1, side);
						merged++;
						// Otherwise, add a new quad.
					}
					else {
						glm::vec3 pos1(x, y, z + 1);
						glm::vec3 pos2(x, y, z);
						glm::vec3 pos3(x, y + 1, z);
						glm::vec3 pos4(x, y + 1, z + 1);

						glm::vec2 uv1((0 + side) / 16.0, 1);
						glm::vec2 uv2((0 + side) / 16.0, 0);
						glm::vec2 uv3((1 + side) / 16.0, 0);
						glm::vec2 uv4((1 + side) / 16.0, 1);
						glm::vec3 nm(-1.0f, 0.0f, 0.0f);

						// calculate tangent/bitangent vectors of both triangles
						glm::vec3 tangent1, bitangent1;
						glm::vec3 tangent2, bitangent2;
						// triangle 1
						// ----------
						glm::vec3 edge1 = pos2 - pos1;
						glm::vec3 edge2 = pos3 - pos1;
						glm::vec2 deltaUV1 = uv2 - uv1;
						glm::vec2 deltaUV2 = uv3 - uv1;

						GLfloat f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);

						tangent1.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
						tangent1.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
						tangent1.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);
						tangent1 = glm::normalize(tangent1);

						bitangent1.x = f * (-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x);
						bitangent1.y = f * (-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y);
						bitangent1.z = f * (-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z);
						bitangent1 = glm::normalize(bitangent1);

						// triangle 2
						// ----------
						edge1 = pos3 - pos1;
						edge2 = pos4 - pos1;
						deltaUV1 = uv3 - uv1;
						deltaUV2 = uv4 - uv1;

						f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);

						tangent2.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
						tangent2.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
						tangent2.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);
						tangent2 = glm::normalize(tangent2);


						bitangent2.x = f * (-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x);
						bitangent2.y = f * (-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y);
						bitangent2.z = f * (-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z);
						bitangent2 = glm::normalize(bitangent2);

						normal[i] = -1;
						tangent[i] = tangent1;
						vertex[i++] = byte4(x, y, z, side);
						normal[i] = -1;
						tangent[i] = tangent1;
						vertex[i++] = byte4(x, y, z + 1, side);
						normal[i] = -1;
						tangent[i] = tangent1;
						vertex[i++] = byte4(x, y + 1, z, side);
						normal[i] = -1;
						tangent[i] = tangent2;
						vertex[i++] = byte4(x, y + 1, z, side);
						normal[i] = -1;
						tangent[i] = tangent2;
						vertex[i++] = byte4(x, y, z + 1, side);
						normal[i] = -1;
						tangent[i] = tangent2;
						vertex[i++] = byte4(x, y + 1, z + 1, side);
						
					}

					vis = true;
				}
			}
		}

		// View from positive x

		for (int x = 0; x < CX; x++) {
			for (int y = 0; y < CY; y++) {
				for (int z = 0; z < CZ; z++) {
					if (isblocked(x, y, z, x + 1, y, z)) {
						vis = false;
						continue;
					}

					uint8_t top = blk[x][y][z];
					uint8_t bottom = blk[x][y][z];
					uint8_t side = blk[x][y][z];

					// Grass block has dirt sides and bottom
					if (top == 2) {
						bottom = 3;
						side = 9;
						// Wood blocks have rings on top and bottom
					}
					else if (top == 5) {
						top = bottom = 12;
					}

					if (vis && z != 0 && blk[x][y][z] == blk[x][y][z - 1]) {
						vertex[i - 4] = byte4(x + 1, y, z + 1, side);
						vertex[i - 2] = byte4(x + 1, y + 1, z + 1, side);
						vertex[i - 1] = byte4(x + 1, y, z + 1, side);
						merged++;
					}
					else {
						glm::vec3 pos1(x+1, y, z + 1);
						glm::vec3 pos2(x+1, y, z);
						glm::vec3 pos3(x+1, y + 1, z);
						glm::vec3 pos4(x+1, y + 1, z + 1);

						glm::vec2 uv1((0 + side) / 16.0, 1);
						glm::vec2 uv2((0 + side) / 16.0, 0);
						glm::vec2 uv3((1 + side) / 16.0, 0);
						glm::vec2 uv4((1 + side) / 16.0, 1);
						glm::vec3 nm(1.0f, 0.0f, 0.0f);

						// calculate tangent/bitangent vectors of both triangles
						glm::vec3 tangent1, bitangent1;
						glm::vec3 tangent2, bitangent2;
						// triangle 1
						// ----------
						glm::vec3 edge1 = pos2 - pos1;
						glm::vec3 edge2 = pos3 - pos1;
						glm::vec2 deltaUV1 = uv2 - uv1;
						glm::vec2 deltaUV2 = uv3 - uv1;

						GLfloat f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);

						tangent1.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
						tangent1.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
						tangent1.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);
						tangent1 = glm::normalize(tangent1);

						bitangent1.x = f * (-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x);
						bitangent1.y = f * (-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y);
						bitangent1.z = f * (-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z);
						bitangent1 = glm::normalize(bitangent1);

						// triangle 2
						// ----------
						edge1 = pos3 - pos1;
						edge2 = pos4 - pos1;
						deltaUV1 = uv3 - uv1;
						deltaUV2 = uv4 - uv1;

						f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);

						tangent2.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
						tangent2.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
						tangent2.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);
						tangent2 = glm::normalize(tangent2);


						bitangent2.x = f * (-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x);
						bitangent2.y = f * (-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y);
						bitangent2.z = f * (-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z);
						bitangent2 = glm::normalize(bitangent2);

						normal[i] = 1;
						tangent[i] = tangent1;
						vertex[i++] = byte4(x + 1, y, z, side);
						normal[i] = 1;
						tangent[i] = tangent1;
						vertex[i++] = byte4(x + 1, y + 1, z, side);
						normal[i] = 1;
						tangent[i] = tangent1;
						vertex[i++] = byte4(x + 1, y, z + 1, side);
						normal[i] = 1;
						tangent[i] = tangent2;
						vertex[i++] = byte4(x + 1, y + 1, z, side);
						normal[i] = 1;
						tangent[i] = tangent2;
						vertex[i++] = byte4(x + 1, y + 1, z + 1, side);
						normal[i] = 1;
						tangent[i] = tangent2;
						vertex[i++] = byte4(x + 1, y, z + 1, side);
					}
					vis = true;
				}
			}
		}

		// View from negative y

		for (int x = 0; x < CX; x++) {
			for (int y = CY - 1; y >= 0; y--) {
				for (int z = 0; z < CZ; z++) {
					if (isblocked(x, y, z, x, y - 1, z)) {
						vis = false;
						continue;
					}

					uint8_t top = blk[x][y][z];
					uint8_t bottom = blk[x][y][z];

					// Grass block has dirt sides and bottom
					if (top == 2) {
						bottom = 3;
						//side = 9;
						// Wood blocks have rings on top and bottom
					}
					else if (top == 5) {
						top = bottom = 12;
					}

					if (vis && z != 0 && blk[x][y][z] == blk[x][y][z - 1]) {
						vertex[i - 4] = byte4(x, y, z + 1, bottom + 128);
						vertex[i - 2] = byte4(x + 1, y, z + 1, bottom + 128);
						vertex[i - 1] = byte4(x, y, z + 1, bottom + 128);
						merged++;
					}
					else {
						glm::vec3 pos1(x + 1, y, z);
						glm::vec3 pos2(x, y, z);
						glm::vec3 pos3(x, y, z + 1);
						glm::vec3 pos4(x + 1, y, z + 1);

						glm::vec2 uv1((1 + bottom) / 16.0, 0);
						glm::vec2 uv2((0 + bottom) / 16.0, 0);
						glm::vec2 uv3((0 + bottom) / 16.0, 1);
						glm::vec2 uv4((1 + bottom) / 16.0, 1);
						glm::vec3 nm(0.0f ,-1.0f, 0.0f);

						// calculate tangent/bitangent vectors of both triangles
						glm::vec3 tangent1, bitangent1;
						glm::vec3 tangent2, bitangent2;
						// triangle 1
						// ----------
						glm::vec3 edge1 = pos2 - pos1;
						glm::vec3 edge2 = pos3 - pos1;
						glm::vec2 deltaUV1 = uv2 - uv1;
						glm::vec2 deltaUV2 = uv3 - uv1;

						GLfloat f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);

						tangent1.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
						tangent1.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
						tangent1.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);
						tangent1 = glm::normalize(tangent1);

						bitangent1.x = f * (-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x);
						bitangent1.y = f * (-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y);
						bitangent1.z = f * (-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z);
						bitangent1 = glm::normalize(bitangent1);

						// triangle 2
						// ----------
						edge1 = pos3 - pos1;
						edge2 = pos4 - pos1;
						deltaUV1 = uv3 - uv1;
						deltaUV2 = uv4 - uv1;

						f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);

						tangent2.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
						tangent2.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
						tangent2.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);
						tangent2 = glm::normalize(tangent2);


						bitangent2.x = f * (-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x);
						bitangent2.y = f * (-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y);
						bitangent2.z = f * (-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z);
						bitangent2 = glm::normalize(bitangent2);
						normal[i] = -2;
						tangent[i] = tangent1;
						vertex[i++] = byte4(x, y, z, bottom + 128);
						normal[i] = -2;
						tangent[i] = tangent1;
						vertex[i++] = byte4(x + 1, y, z, bottom + 128);
						normal[i] = -2;
						tangent[i] = tangent1;
						vertex[i++] = byte4(x, y, z + 1, bottom + 128);
						normal[i] = -2;
						tangent[i] = tangent2;
						vertex[i++] = byte4(x + 1, y, z, bottom + 128);
						normal[i] = -2;
						tangent[i] = tangent2;
						vertex[i++] = byte4(x + 1, y, z + 1, bottom + 128);
						normal[i] = -2;
						tangent[i] = tangent2;
						vertex[i++] = byte4(x, y, z + 1, bottom + 128);
					}
					vis = true;
				}
			}
		}

		// View from positive y

		for (int x = 0; x < CX; x++) {
			for (int y = 0; y < CY; y++) {
				for (int z = 0; z < CZ; z++) {
					if (isblocked(x, y, z, x, y + 1, z)) {
						vis = false;
						continue;
					}

					uint8_t top = blk[x][y][z];
					uint8_t bottom = blk[x][y][z];

					// Grass block has dirt sides and bottom
					if (top == 2) {
						bottom = 3;
						//side = 9;
						// Wood blocks have rings on top and bottom
					}
					else if (top == 5) {
						top = bottom = 12;
					}

					if (vis && z != 0 && blk[x][y][z] == blk[x][y][z - 1]) {
						vertex[i - 5] = byte4(x, y + 1, z + 1, top + 128);
						vertex[i - 2] = byte4(x, y + 1, z + 1, top + 128);
						vertex[i - 1] = byte4(x + 1, y + 1, z + 1, top + 128);
						merged++;
					}
					else {
						glm::vec3 pos1(x, y + 1, z + 1);
						glm::vec3 pos2(x, y + 1, z);
						glm::vec3 pos3(x + 1, y + 1, z);
						glm::vec3 pos4(x + 1, y + 1, z + 1);

						glm::vec2 uv1((0 + top) / 16.0, 1);
						glm::vec2 uv2((0 + top) / 16.0, 0);
						glm::vec2 uv3((1 + top) / 16.0, 0);
						glm::vec2 uv4((1 + top) / 16.0, 1);
						glm::vec3 nm(0.0f, 1.0f, 0.0f);

						// calculate tangent/bitangent vectors of both triangles
						glm::vec3 tangent1, bitangent1;
						glm::vec3 tangent2, bitangent2;
						// triangle 1
						// ----------
						glm::vec3 edge1 = pos2 - pos1;
						glm::vec3 edge2 = pos3 - pos1;
						glm::vec2 deltaUV1 = uv2 - uv1;
						glm::vec2 deltaUV2 = uv3 - uv1;

						GLfloat f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);

						tangent1.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
						tangent1.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
						tangent1.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);
						tangent1 = glm::normalize(tangent1);

						bitangent1.x = f * (-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x);
						bitangent1.y = f * (-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y);
						bitangent1.z = f * (-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z);
						bitangent1 = glm::normalize(bitangent1);

						// triangle 2
						// ----------
						edge1 = pos3 - pos1;
						edge2 = pos4 - pos1;
						deltaUV1 = uv3 - uv1;
						deltaUV2 = uv4 - uv1;

						f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);

						tangent2.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
						tangent2.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
						tangent2.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);
						tangent2 = glm::normalize(tangent2);


						bitangent2.x = f * (-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x);
						bitangent2.y = f * (-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y);
						bitangent2.z = f * (-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z);
						bitangent2 = glm::normalize(bitangent2);
						normal[i] = 2;
						tangent[i] = tangent1;
						vertex[i++] = byte4(x, y + 1, z, top + 128);
						normal[i] = 2;
						tangent[i] = tangent1;
						vertex[i++] = byte4(x, y + 1, z + 1, top + 128);
						normal[i] = 2;
						tangent[i] = tangent1;
						vertex[i++] = byte4(x + 1, y + 1, z, top + 128);
						normal[i] = 2;
						tangent[i] = tangent2;
						vertex[i++] = byte4(x + 1, y + 1, z, top + 128);
						normal[i] = 2;
						tangent[i] = tangent2;
						vertex[i++] = byte4(x, y + 1, z + 1, top + 128);
						normal[i] = 2;
						tangent[i] = tangent2;
						vertex[i++] = byte4(x + 1, y + 1, z + 1, top + 128);
					}
					vis = true;
				}
			}
		}

		// View from negative z

		for (int x = 0; x < CX; x++) {
			for (int z = CZ - 1; z >= 0; z--) {
				for (int y = 0; y < CY; y++) {
					if (isblocked(x, y, z, x, y, z - 1)) {
						vis = false;
						continue;
					}

					uint8_t top = blk[x][y][z];
					uint8_t bottom = blk[x][y][z];
					uint8_t side = blk[x][y][z];

					// Grass block has dirt sides and bottom
					if (top == 2) {
						bottom = 3;
						side = 9;
						// Wood blocks have rings on top and bottom
					}
					else if (top == 5) {
						top = bottom = 12;
					}

					if (vis && y != 0 && blk[x][y][z] == blk[x][y - 1][z]) {
						vertex[i - 5] = byte4(x, y + 1, z, side);
						vertex[i - 3] = byte4(x, y + 1, z, side);
						vertex[i - 2] = byte4(x + 1, y + 1, z, side);
						merged++;
					}
					else {
						glm::vec3 pos1(x, y + 1, z);
						glm::vec3 pos2(x, y, z);
						glm::vec3 pos3(x + 1, y, z);
						glm::vec3 pos4(x + 1, y + 1, z);

						glm::vec2 uv1((0 + side) / 16.0, 1);
						glm::vec2 uv2((0 + side) / 16.0, 0);
						glm::vec2 uv3((1 + side) / 16.0, 0);
						glm::vec2 uv4((1 + side) / 16.0, 1);
						glm::vec3 nm(0.0f, 0.0f ,-1.0f);

						// calculate tangent/bitangent vectors of both triangles
						glm::vec3 tangent1, bitangent1;
						glm::vec3 tangent2, bitangent2;
						// triangle 1
						// ----------
						glm::vec3 edge1 = pos2 - pos1;
						glm::vec3 edge2 = pos3 - pos1;
						glm::vec2 deltaUV1 = uv2 - uv1;
						glm::vec2 deltaUV2 = uv3 - uv1;

						GLfloat f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);

						tangent1.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
						tangent1.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
						tangent1.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);
						tangent1 = glm::normalize(tangent1);

						bitangent1.x = f * (-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x);
						bitangent1.y = f * (-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y);
						bitangent1.z = f * (-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z);
						bitangent1 = glm::normalize(bitangent1);

						// triangle 2
						// ----------
						edge1 = pos3 - pos1;
						edge2 = pos4 - pos1;
						deltaUV1 = uv3 - uv1;
						deltaUV2 = uv4 - uv1;

						f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);

						tangent2.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
						tangent2.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
						tangent2.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);
						tangent2 = glm::normalize(tangent2);


						bitangent2.x = f * (-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x);
						bitangent2.y = f * (-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y);
						bitangent2.z = f * (-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z);
						bitangent2 = glm::normalize(bitangent2);
						normal[i] = -3;
						tangent[i] = tangent1;
						vertex[i++] = byte4(x, y, z, side);
						normal[i] = -3;
						tangent[i] = tangent1;
						vertex[i++] = byte4(x, y + 1, z, side);
						normal[i] = -3;
						tangent[i] = tangent1;
						vertex[i++] = byte4(x + 1, y, z, side);
						normal[i] = -3;
						tangent[i] = tangent2;
						vertex[i++] = byte4(x, y + 1, z, side);
						normal[i] = -3;
						tangent[i] = tangent2;
						vertex[i++] = byte4(x + 1, y + 1, z, side);
						normal[i] = -3;
						tangent[i] = tangent2;
						vertex[i++] = byte4(x + 1, y, z, side);
					}
					vis = true;
				}
			}
		}

		// View from positive z

		for (int x = 0; x < CX; x++) {
			for (int z = 0; z < CZ; z++) {
				for (int y = 0; y < CY; y++) {
					if (isblocked(x, y, z, x, y, z + 1)) {
						vis = false;
						continue;
					}

					uint8_t top = blk[x][y][z];
					uint8_t bottom = blk[x][y][z];
					uint8_t side = blk[x][y][z];

					// Grass block has dirt sides and bottom
					if (top == 2) {
						bottom = 3;
						side = 9;
						// Wood blocks have rings on top and bottom
					}
					else if (top == 5) {
						top = bottom = 12;
					}

					if (vis && y != 0 && blk[x][y][z] == blk[x][y - 1][z]) {
						vertex[i - 4] = byte4(x, y + 1, z + 1, side);
						vertex[i - 3] = byte4(x, y + 1, z + 1, side);
						vertex[i - 1] = byte4(x + 1, y + 1, z + 1, side);
						merged++;
					}
					else {
						glm::vec3 pos1(x + 1, y, z + 1);
						glm::vec3 pos2(x, y, z + 1);
						glm::vec3 pos3(x, y + 1, z + 1);
						glm::vec3 pos4(x + 1, y + 1, z + 1);

						glm::vec2 uv1((1 + side) / 16.0, 0);
						glm::vec2 uv2((0 + side) / 16.0, 0);
						glm::vec2 uv3((0 + side) / 16.0, 1);
						glm::vec2 uv4((1 + side) / 16.0, 1);
						glm::vec3 nm(0.0f, 0.0f, 1.0f);

						// calculate tangent/bitangent vectors of both triangles
						glm::vec3 tangent1, bitangent1;
						glm::vec3 tangent2, bitangent2;
						// triangle 1
						// ----------
						glm::vec3 edge1 = pos2 - pos1;
						glm::vec3 edge2 = pos3 - pos1;
						glm::vec2 deltaUV1 = uv2 - uv1;
						glm::vec2 deltaUV2 = uv3 - uv1;

						GLfloat f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);

						tangent1.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
						tangent1.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
						tangent1.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);
						tangent1 = glm::normalize(tangent1);

						bitangent1.x = f * (-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x);
						bitangent1.y = f * (-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y);
						bitangent1.z = f * (-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z);
						bitangent1 = glm::normalize(bitangent1);

						// triangle 2
						// ----------
						edge1 = pos3 - pos1;
						edge2 = pos4 - pos1;
						deltaUV1 = uv3 - uv1;
						deltaUV2 = uv4 - uv1;

						f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);

						tangent2.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
						tangent2.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
						tangent2.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);
						tangent2 = glm::normalize(tangent2);


						bitangent2.x = f * (-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x);
						bitangent2.y = f * (-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y);
						bitangent2.z = f * (-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z);
						bitangent2 = glm::normalize(bitangent2);
						normal[i] = 3;
						tangent[i] = tangent1;
						vertex[i++] = byte4(x, y, z + 1, side);
						normal[i] = 3;
						tangent[i] = tangent1;
						vertex[i++] = byte4(x + 1, y, z + 1, side);
						normal[i] = 3;
						tangent[i] = tangent1;
						vertex[i++] = byte4(x, y + 1, z + 1, side);
						normal[i] = 3;
						tangent[i] = tangent2;
						vertex[i++] = byte4(x, y + 1, z + 1, side);
						normal[i] = 3;
						tangent[i] = tangent2;
						vertex[i++] = byte4(x + 1, y, z + 1, side);
						normal[i] = 3;
						tangent[i] = tangent2;
						vertex[i++] = byte4(x + 1, y + 1, z + 1, side);
					}
					vis = true;
				}
			}
		}

		changed = false;
		elements = i;

		// If this chunk is empty, no need to allocate a chunk slot.
		if (!elements)
			return;

		// If we don't have an active slot, find one
		if (chunk_slot[slot] != this) {
			int lru = 0;
			for (int i = 0; i < CHUNKSLOTS; i++) {
				// If there is an empty slot, use it
				if (!chunk_slot[i]) {
					lru = i;
					break;
				}
				// Otherwise try to find the least recently used slot
				if (chunk_slot[i]->lastused < chunk_slot[lru]->lastused)
					lru = i;
			}

			// If the slot is empty, create a new VBO
			if (!chunk_slot[lru]) {
				glGenBuffers(1, &vbo);
				glGenBuffers(1, &vbo_normal);
				glGenBuffers(1, &vbo_tangent);
				glGenBuffers(1, &vbo_bitangent);
				// Otherwise, steal it from the previous slot owner
			}
			else {
				vbo = chunk_slot[lru]->vbo;
				vbo_normal = chunk_slot[lru]->vbo_normal;
				vbo_tangent = chunk_slot[lru]->vbo_tangent;
				vbo_bitangent = chunk_slot[lru]->vbo_bitangent;
				chunk_slot[lru]->changed = true;
			}

			slot = lru;
			chunk_slot[slot] = this;
		}

		// Upload vertices

		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glBufferData(GL_ARRAY_BUFFER, i * sizeof *vertex, vertex, GL_STATIC_DRAW);
		glBindBuffer(GL_ARRAY_BUFFER, vbo_normal);
		glBufferData(GL_ARRAY_BUFFER, i * sizeof *normal, normal, GL_STATIC_DRAW);
		glBindBuffer(GL_ARRAY_BUFFER, vbo_tangent);
		glBufferData(GL_ARRAY_BUFFER, i * sizeof *tangent, tangent, GL_STATIC_DRAW);
		glBindBuffer(GL_ARRAY_BUFFER, vbo_bitangent);
		glBufferData(GL_ARRAY_BUFFER, i * sizeof *bitangent, bitangent, GL_STATIC_DRAW);
	}

	void render() {
		if (changed)
			update();

		lastused = now;

		if (!elements)
			return;

		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glEnableVertexAttribArray(attribute_coord);
		glVertexAttribPointer(attribute_coord, 4, GL_BYTE, GL_FALSE, 0, 0);
		glBindBuffer(GL_ARRAY_BUFFER, vbo_normal);
		glEnableVertexAttribArray(normal_coord);
		glVertexAttribPointer(normal_coord, 1, GL_FLOAT, GL_FALSE, 0, 0);
		glBindBuffer(GL_ARRAY_BUFFER, vbo_tangent);
		glEnableVertexAttribArray(tangent_coord);
		glVertexAttribPointer(tangent_coord, 3, GL_FLOAT, GL_FALSE, 0, 0);
		glBindBuffer(GL_ARRAY_BUFFER, vbo_bitangent);
		glEnableVertexAttribArray(bitangent_coord);
		glVertexAttribPointer(bitangent_coord, 3, GL_FLOAT, GL_FALSE, 0, 0);
		glDrawArrays(GL_TRIANGLES, 0, elements);
	}
};

struct superchunk {
	chunk *c[SCX][SCY][SCZ];
	time_t seed;

	superchunk() {
		seed = time(NULL);
		for (int x = 0; x < SCX; x++)
			for (int y = 0; y < SCY; y++)
				for (int z = 0; z < SCZ; z++)
					c[x][y][z] = new chunk(x - SCX / 2, y - SCY / 2, z - SCZ / 2);

		for (int x = 0; x < SCX; x++)
			for (int y = 0; y < SCY; y++)
				for (int z = 0; z < SCZ; z++) {
					if (x > 0)
						c[x][y][z]->left = c[x - 1][y][z];
					if (x < SCX - 1)
						c[x][y][z]->right = c[x + 1][y][z];
					if (y > 0)
						c[x][y][z]->below = c[x][y - 1][z];
					if (y < SCY - 1)
						c[x][y][z]->above = c[x][y + 1][z];
					if (z > 0)
						c[x][y][z]->front = c[x][y][z - 1];
					if (z < SCZ - 1)
						c[x][y][z]->back = c[x][y][z + 1];
				}
	}

	uint8_t get(int x, int y, int z) const {
		int cx = (x + CX * (SCX / 2)) / CX;
		int cy = (y + CY * (SCY / 2)) / CY;
		int cz = (z + CZ * (SCZ / 2)) / CZ;

		if (cx < 0 || cx >= SCX || cy < 0 || cy >= SCY || cz <= 0 || cz >= SCZ)
			return 0;

		return c[cx][cy][cz]->get(x & (CX - 1), y & (CY - 1), z & (CZ - 1));
	}

	void set(int x, int y, int z, uint8_t type) {
		int cx = (x + CX * (SCX / 2)) / CX;
		int cy = (y + CY * (SCY / 2)) / CY;
		int cz = (z + CZ * (SCZ / 2)) / CZ;

		if (cx < 0 || cx >= SCX || cy < 0 || cy >= SCY || cz <= 0 || cz >= SCZ)
			return;

		c[cx][cy][cz]->set(x & (CX - 1), y & (CY - 1), z & (CZ - 1), type);
	}

	void render(const glm::mat4 &pv, const glm::mat4 &lightSpaceMatrix, const int &whichprogram) {
		float ud = 1 >> 32;
		int ux = -1;
		int uy = -1;
		int uz = -1;

		for (int x = 0; x < SCX; x++) {
			for (int y = 0; y < SCY; y++) {
				for (int z = 0; z < SCZ; z++) {
					glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(c[x][y][z]->ax * CX, c[x][y][z]->ay * CY, c[x][y][z]->az * CZ));
					glm::mat4 mvp = pv * model;
					glm::mat4 LSM = lightSpaceMatrix * model;

					// Is this chunk on the screen?
					glm::vec4 center = mvp * glm::vec4(CX / 2, CY / 2, CZ / 2, 1);

					float d = glm::length(center);
					center.x /= center.w;
					center.y /= center.w;

					// If it is behind the camera, don't bother drawing it
					if (center.z < -CY / 2)
						continue;

					// If it is outside the screen, don't bother drawing it
					if (fabsf(center.x) > 1 + fabsf(CY * 2 / center.w) || fabsf(center.y) > 1 + fabsf(CY * 2 / center.w))
						continue;

					// If this chunk is not initialized, skip it
					if (!c[x][y][z]->initialized) {
						// But if it is the closest to the camera, mark it for initialization
						if (ux < 0 || d < ud) {
							ud = d;
							ux = x;
							uy = y;
							uz = z;
						}
						continue;
					}
					GLint uniform_model = glGetUniformLocation(program, "model");
					glUniformMatrix4fv(uniform_model, 1, GL_FALSE, glm::value_ptr(model));
					if (whichprogram == 0) {
						glUniformMatrix4fv(uniform_mvp, 1, GL_FALSE, glm::value_ptr(mvp));
						glUniformMatrix4fv(uniform_lightspacematrix_normal, 1, GL_FALSE, glm::value_ptr(LSM));
					}else
					if (whichprogram == 1)
						glUniformMatrix4fv(uniform_lightspacematrix_depth, 1, GL_FALSE, glm::value_ptr(LSM));

					c[x][y][z]->render();
				}
			}
		}

		if (ux >= 0) {
			c[ux][uy][uz]->noise();
			if (c[ux][uy][uz]->left)
				c[ux][uy][uz]->left->noise();
			if (c[ux][uy][uz]->right)
				c[ux][uy][uz]->right->noise();
			if (c[ux][uy][uz]->below)
				c[ux][uy][uz]->below->noise();
			if (c[ux][uy][uz]->above)
				c[ux][uy][uz]->above->noise();
			if (c[ux][uy][uz]->front)
				c[ux][uy][uz]->front->noise();
			if (c[ux][uy][uz]->back)
				c[ux][uy][uz]->back->noise();
			c[ux][uy][uz]->initialized = true;
		}
	}
};

static superchunk *world;

static void update_vectors() {
	fforward.x = sinf(angle.x);
	fforward.y = 0;
	fforward.z = cosf(angle.x);

	rright.x = -cosf(angle.x);
	rright.y = 0;
	rright.z = sinf(angle.x);

	lookat.x = sinf(angle.x) * cosf(angle.y);
	lookat.y = sinf(angle.y);
	lookat.z = cosf(angle.x) * cosf(angle.y);

	up = glm::cross(rright, lookat);
}

static float fract(float value) {
	float f = value - floorf(value);
	if (f > 0.5)
		return 1 - f;
	else
		return f;
}

GLint get_uniform(GLuint program, const char *name) {
	GLint uniform = glGetUniformLocation(program, name);
	if (uniform == -1)
		fprintf(stderr, "Could not bind uniform %s\n", name);
	return uniform;
}

void checkError(unsigned int shader, const bool type) {
	int success;
	char infoLog[512];
	if (type == false) {
		glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
		if (!success) {
			glGetShaderInfoLog(shader, 512, NULL, infoLog);
			std::cout << infoLog << std::endl;
		}
	}
	else
		if (type == true) {
			glGetProgramiv(shader, GL_LINK_STATUS, &success);
			if (!success) {
				glGetProgramInfoLog(shader, 512, NULL, infoLog);
				std::cout << infoLog << std::endl;
			}
		}
}

GLuint create_program(const char *vertexfile, const char *fragmentfile) {
	std::string vertexCode, fragmentCode;
	std::ifstream vertexFile, fragmentFile;
	vertexFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
	fragmentFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);

	try {
		vertexFile.open(vertexfile);
		fragmentFile.open(fragmentfile);

		std::stringstream vertexStream, fragmentStream;
		vertexStream << vertexFile.rdbuf();
		fragmentStream << fragmentFile.rdbuf();
		vertexFile.close();
		fragmentFile.close();
		vertexCode = vertexStream.str();
		fragmentCode = fragmentStream.str();
	}
	catch (std::ifstream::failure) {
		std::cout << "ERROR::SHADER::FILE_NOT_SUCCESSFULY_READ" << std::endl;
		return 0;
	}
	const char* vShaderCode = vertexCode.c_str();
	const char* fShaderCode = fragmentCode.c_str();

	GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vertexShader, 1, &vShaderCode, NULL);
	glCompileShader(vertexShader);
	checkError(vertexShader, false);
	GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fragmentShader, 1, &fShaderCode, NULL);
	glCompileShader(fragmentShader);
	checkError(fragmentShader, false);
	GLuint shaderProgram = glCreateProgram();
	glAttachShader(shaderProgram, vertexShader);
	glAttachShader(shaderProgram, fragmentShader);
	glLinkProgram(shaderProgram);
	checkError(shaderProgram, true);
	glDeleteShader(vertexShader);
	glDeleteShader(fragmentShader);
	vertexShader = -1;
	fragmentShader = -1;

	return shaderProgram;
}

unsigned int loadTexture(char const * path)
{
	unsigned int textureID;
	glGenTextures(1, &textureID);

	int width, height, nrComponents;
	unsigned char *data = stbi_load(path, &width, &height, &nrComponents, 0);
	if (data)
	{
		GLenum format;
		if (nrComponents == 1)
			format = GL_RED;
		else if (nrComponents == 3)
			format = GL_RGB;
		else if (nrComponents == 4)
			format = GL_RGBA;

		glBindTexture(GL_TEXTURE_2D, textureID);
		glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
		glGenerateMipmap(GL_TEXTURE_2D);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, format == GL_RGBA ? GL_CLAMP_TO_EDGE : GL_REPEAT); // for this tutorial: use GL_CLAMP_TO_EDGE to prevent semi-transparent borders. Due to interpolation it takes texels from next repeat 
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, format == GL_RGBA ? GL_CLAMP_TO_EDGE : GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		stbi_image_free(data);
	}
	else
	{
		std::cout << "Texture failed to load at path: " << path << std::endl;
		stbi_image_free(data);
	}

	return textureID;
}

int main()
{
	for (int x = 0; x < SCX; x++)
		for (int z = 0; z < SCZ; z++)
		{
			ChunkColumnStorage chunk1 = c->Generate(x + 40, z + 10, GeneratorSettings());
			/*for (int i = 0; i < 16; i++) {
			for (int j = 0; j < 16; j++)
			cout << chunk1.Biomes[j * 16 + i];
			cout << endl;
			}*/
			for (int i = 0; i < 16; i++)
			for (int j = 0; j < 16; j++)
			for (int y = 0; y < CY*SCY; y++)
			{
			blocks[x*CX + i][y][z*CZ + j] = chunk1(i, y, j).Id;
			}
		}
	delete c;
	// glfw: initialize and configure
	// ------------------------------
	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#ifdef __APPLE__
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // uncomment this statement to fix compilation on OS X
#endif

														 // glfw window creation
														 // --------------------
	GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "LearnOpenGL", NULL, NULL);
	if (window == NULL)
	{
		std::cout << "Failed to create GLFW window" << std::endl;
		glfwTerminate();
		return -1;
	}
	glfwMakeContextCurrent(window);
	glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
	glfwSetCursorPosCallback(window, mouse_callback);
	glfwSetScrollCallback(window, scroll_callback);

	// tell GLFW to capture our mouse
	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

	// glad: load all OpenGL function pointers
	// ---------------------------------------
	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
	{
		std::cout << "Failed to initialize GLAD" << std::endl;
		return -1;
	}
	glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);

	// build and compile our shader zprogram
	// ------------------------------------
	program = create_program("glcraft.vert", "glcraft.frag");
	depthProgram = create_program("simple.vert", "simple.frag");
	depthTestProgram = create_program("simpleTexture.vert", "simpleTexture.frag");

	if (program == 0 || depthProgram == 0)
		return 0;

	glUseProgram(depthProgram);
	uniform_lightspacematrix_depth = get_uniform(depthProgram, "lightSpaceMatrix");

	glUseProgram(program);
	uniform_mvp = get_uniform(program, "mvp");
	uniform_viewpos = get_uniform(program, "viewPos");
	uniform_lightspacematrix_normal = get_uniform(program, "lightSpaceMatrix");
	uniform_texture = get_uniform(program, "texture");
	uniform_normalMap = get_uniform(program, "normalMap");
	uniform_shadow = get_uniform(program, "shadow");
	uniform_dirlight.direction = get_uniform(program, "dirlight.direction");
	uniform_dirlight.ambient = get_uniform(program, "dirlight.ambient");
	uniform_dirlight.diffuse = get_uniform(program, "dirlight.diffuse");
	uniform_dirlight.specular = get_uniform(program, "dirlight.specular");

	dirlight.ambient = glm::vec3(0.2f);
	dirlight.diffuse = glm::vec3(0.8f);
	dirlight.specular = glm::vec3(.0f);

	if (uniform_mvp == -1 || uniform_viewpos == -1 ||
		uniform_dirlight.direction == -1 || uniform_dirlight.ambient == -1 ||
		uniform_dirlight.diffuse == -1 || uniform_dirlight.specular == -1)
		return 0;

	/* Create and upload the texture */

	glActiveTexture(GL_TEXTURE0);
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	/*glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, textures.width, textures.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, textures.pixel_data);
	glGenerateMipmap(GL_TEXTURE_2D);*/
	int width, height, nrChannels;
	// The FileSystem::getPath(...) is part of the GitHub repository so we can find files on any IDE/platform; replace it with your own image path.
	unsigned char *data = stbi_load("textures.png", &width, &height, &nrChannels, 0);
	if (data)
	{
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
		glGenerateMipmap(GL_TEXTURE_2D);
	}
	else
	{
		std::cout << "Failed to load texture" << std::endl;
	}
	stbi_image_free(data);

	//glActiveTexture(GL_TEXTURE1);
	glGenTextures(1, &normalMap);
	glBindTexture(GL_TEXTURE_2D, normalMap);
	unsigned char *data2 = stbi_load("normaltextures.png", &width, &height, &nrChannels, 0);
	if (data2)
	{
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data2);
		glGenerateMipmap(GL_TEXTURE_2D);
	}
	else
	{
		std::cout << "Failed to load texture" << std::endl;
	}
	stbi_image_free(data2);

	/* Create the world */

	world = new superchunk;

	position = glm::vec3(0, CY + 1, 0);
	angle = glm::vec3(0, -0.5, 0);
	update_vectors();

	/* Create a VBO for the cursor */

	glGenBuffers(1, &cursor_vbo);

	/* OpenGL settings that do not change while running this program */

	glUseProgram(program);
	glUniform1i(uniform_texture, 0);
	glClearColor(0.6, 0.8, 1.0, 0.0);
	glEnable(GL_CULL_FACE);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); // Use GL_NEAREST_MIPMAP_LINEAR if you want to use mipmaps
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glPolygonOffset(1, 1);

	glEnableVertexAttribArray(attribute_coord);
	glEnableVertexAttribArray(normal_coord);
	glEnableVertexAttribArray(tangent_coord);
	glEnableVertexAttribArray(bitangent_coord);

	unsigned int VAO;
	glGenVertexArrays(1, &VAO);

	glBindVertexArray(VAO);

	glEnableVertexAttribArray(0);

	//create depth texture
	GLuint depthMapFBO;
	glGenFramebuffers(1, &depthMapFBO);
	const GLuint SHADOW_WIDTH = 9000, SHADOW_HEIGHT = 9000;
	GLuint depthMap;
	glGenTextures(1, &depthMap);
	glBindTexture(GL_TEXTURE_2D, depthMap);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, SHADOW_WIDTH, SHADOW_HEIGHT, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	GLfloat borderColor[] = { 1.0, 1.0, 1.0, 1.0 };
	glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

	glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthMap, 0);
	glDrawBuffer(GL_NONE);
	glReadBuffer(GL_NONE);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	glActiveTexture(GL_TEXTURE0);
	glUniform1i(uniform_texture, 0);
	glBindTexture(GL_TEXTURE_2D, texture);

	// render loop
	// -----------
	while (!glfwWindowShouldClose(window))
	{
		// per-frame time logic
		// --------------------
		float currentFrame = glfwGetTime();
		deltaTime = currentFrame - lastFrame;
		lastFrame = currentFrame;

		// input
		// -----
		processInput(window);

		glBindVertexArray(VAO);

		// pass projection matrix to shader (note that in this case it could change every frame)
		glm::mat4 projection = glm::perspective(glm::radians(camera.Zoom), (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.1f, 100.0f);
		// camera/view transformation
		glm::mat4 view = camera.GetViewMatrix();
		glm::mat4 pv = projection * view;

		glUniform3f(uniform_viewpos, camera.Position.x, camera.Position.y, camera.Position.z);
		dirlight.direction = glm::vec3(-1,-2,-1);
		dirlight.UniformSet(uniform_dirlight);
		glm::mat4 lightProjection = glm::ortho(-50.0f, 50.0f, -50.0f, 50.0f, 0.1f, 80.0f);
		glm::vec3 lightPos = glm::normalize(-dirlight.direction);
		lightPos *= 50;
		glm::vec3 lookAtCenter = camera.Front;
		lookAtCenter *= 50;
		lookAtCenter += camera.Position;
		lightPos += lookAtCenter;
		glm::mat4 lightView = glm::lookAt(lightPos, lookAtCenter, glm::vec3(0, 1, 0));
		glm::mat4 lightSpaceMatrix = lightProjection * lightView;

		glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
		glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
		glClear(GL_DEPTH_BUFFER_BIT);
		glUseProgram(depthProgram);
		glEnable(GL_DEPTH_TEST);
		//glEnable(GL_CULL_FACE);
		glDisable(GL_BLEND);
		glDisable(GL_POLYGON_OFFSET_FILL);
		//glCullFace(GL_FRONT);

		world->render(lightSpaceMatrix, lightSpaceMatrix, 1);

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glUseProgram(program);
		glEnable(GL_DEPTH_TEST);
		glEnable(GL_POLYGON_OFFSET_FILL);
		glDisable(GL_CULL_FACE);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);


		//depth texture test
		/*glUseProgram(depthTestProgram);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, depthMap);
		GLuint VAO, VBO, EBO;
		glGenVertexArrays(1, &VAO);
		glGenBuffers(1, &VBO);
		glGenBuffers(1, &EBO);
		glBindVertexArray(VAO);
		glBindBuffer(GL_ARRAY_BUFFER, VBO);
		GLfloat vertices[] = {
		-1, -1,  0,  0,  0,
		1, -1,  0,  1,  0,
		-1,  1,  0,  0,  1,
		1,  1,  0,  1,  1
		};
		GLuint indices[] = {
		0, 1, 2, 1, 2, 3
		};

		glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), 0);
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (void*)(3 * sizeof(GLfloat)));
		glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, NULL);
		glBindVertexArray(0);*/

		/* Then draw chunks */

		glActiveTexture(GL_TEXTURE1);
		glUniform1i(uniform_shadow, 1);
		glBindTexture(GL_TEXTURE_2D, depthMap);
		glActiveTexture(GL_TEXTURE2);
		glUniform1i(uniform_normalMap, 2);
		glBindTexture(GL_TEXTURE_2D, normalMap);
		
		world->render(pv, lightSpaceMatrix, 0);

		/* At which voxel are we looking? */

		if (select_using_depthbuffer) {
			/* Find out coordinates of the center pixel */

			float depth;
			glReadPixels(ww / 2, wh / 2, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);

			glm::vec4 viewport = glm::vec4(0, 0, ww, wh);
			glm::vec3 wincoord = glm::vec3(ww / 2, wh / 2, depth);
			glm::vec3 objcoord = glm::unProject(wincoord, view, projection, viewport);

			/* Find out which block it belongs to */

			mx = objcoord.x;
			my = objcoord.y;
			mz = objcoord.z;
			if (objcoord.x < 0)
				mx--;
			if (objcoord.y < 0)
				my--;
			if (objcoord.z < 0)
				mz--;

			/* Find out which face of the block we are looking at */

			if (glm::fract(objcoord.x) < glm::fract(objcoord.y))
				if (glm::fract(objcoord.x) < glm::fract(objcoord.z))
					face = 0; // X
				else
					face = 2; // Z
			else
				if (glm::fract(objcoord.y) < glm::fract(objcoord.z))
					face = 1; // Y
				else
					face = 2; // Z

			if (face == 0 && lookat.x > 0)
				face += 3;
			if (face == 1 && lookat.y > 0)
				face += 3;
			if (face == 2 && lookat.z > 0)
				face += 3;
		}
		else {
			/* Very naive ray casting algorithm to find out which block we are looking at */

			glm::vec3 testpos = position;
			glm::vec3 prevpos = position;

			for (int i = 0; i < 100; i++) {
				/* Advance from our currect position to the direction we are looking at, in small steps */

				prevpos = testpos;
				testpos += lookat * 0.1f;

				mx = floorf(testpos.x);
				my = floorf(testpos.y);
				mz = floorf(testpos.z);

				/* If we find a block that is not air, we are done */

				if (world->get(mx, my, mz))
					break;
			}

			/* Find out which face of the block we are looking at */

			int px = floorf(prevpos.x);
			int py = floorf(prevpos.y);
			int pz = floorf(prevpos.z);

			if (px > mx)
				face = 0;
			else if (px < mx)
				face = 3;
			else if (py > my)
				face = 1;
			else if (py < my)
				face = 4;
			else if (pz > mz)
				face = 2;
			else if (pz < mz)
				face = 5;

			/* If we are looking at air, move the cursor out of sight */

			if (!world->get(mx, my, mz))
				mx = my = mz = 99999;
		}

		float bx = mx;
		float by = my;
		float bz = mz;

		/* Render a box around the block we are pointing at */

		float box[24][4] = {
			{ bx + 0, by + 0, bz + 0, 14 },
			{ bx + 1, by + 0, bz + 0, 14 },
			{ bx + 0, by + 1, bz + 0, 14 },
			{ bx + 1, by + 1, bz + 0, 14 },
			{ bx + 0, by + 0, bz + 1, 14 },
			{ bx + 1, by + 0, bz + 1, 14 },
			{ bx + 0, by + 1, bz + 1, 14 },
			{ bx + 1, by + 1, bz + 1, 14 },

			{ bx + 0, by + 0, bz + 0, 14 },
			{ bx + 0, by + 1, bz + 0, 14 },
			{ bx + 1, by + 0, bz + 0, 14 },
			{ bx + 1, by + 1, bz + 0, 14 },
			{ bx + 0, by + 0, bz + 1, 14 },
			{ bx + 0, by + 1, bz + 1, 14 },
			{ bx + 1, by + 0, bz + 1, 14 },
			{ bx + 1, by + 1, bz + 1, 14 },

			{ bx + 0, by + 0, bz + 0, 14 },
			{ bx + 0, by + 0, bz + 1, 14 },
			{ bx + 1, by + 0, bz + 0, 14 },
			{ bx + 1, by + 0, bz + 1, 14 },
			{ bx + 0, by + 1, bz + 0, 14 },
			{ bx + 0, by + 1, bz + 1, 14 },
			{ bx + 1, by + 1, bz + 0, 14 },
			{ bx + 1, by + 1, bz + 1, 14 },
		};

		glDisable(GL_POLYGON_OFFSET_FILL);
		glDisable(GL_CULL_FACE);
		glUniformMatrix4fv(uniform_mvp, 1, GL_FALSE, glm::value_ptr(pv));
		glBindBuffer(GL_ARRAY_BUFFER, cursor_vbo);
		glBufferData(GL_ARRAY_BUFFER, sizeof box, box, GL_DYNAMIC_DRAW);
		glVertexAttribPointer(attribute_coord, 4, GL_FLOAT, GL_FALSE, 0, 0);
		//glDrawArrays(GL_LINES, 0, 24);

		/* Draw a cross in the center of the screen */

		float cross[4][4] = {
			{ -0.02, 0, 0, 13 },
			{ +0.02, 0, 0, 13 },
			{ 0, -0.025, 0, 13 },
			{ 0, +0.025, 0, 13 },
		};

		glDisable(GL_DEPTH_TEST);
		glm::mat4 one(1);
		glUniformMatrix4fv(uniform_mvp, 1, GL_FALSE, glm::value_ptr(one));
		glBufferData(GL_ARRAY_BUFFER, sizeof cross, cross, GL_DYNAMIC_DRAW);
		glVertexAttribPointer(attribute_coord, 4, GL_FLOAT, GL_FALSE, 0, 0);
		glDrawArrays(GL_LINES, 0, 4);

		glfwSwapBuffers(window);
		glfwPollEvents();
	}
	
	// glfw: terminate, clearing all previously allocated GLFW resources.
	// ------------------------------------------------------------------
	glfwTerminate();
	return 0;
}

// process all input: query GLFW whether relevant keys are pressed/released this frame and react accordingly
// ---------------------------------------------------------------------------------------------------------
void processInput(GLFWwindow *window)
{
	if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
		glfwSetWindowShouldClose(window, true);

	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
		camera.ProcessKeyboard(FORWARD, deltaTime);
	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
		camera.ProcessKeyboard(BACKWARD, deltaTime);
	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
		camera.ProcessKeyboard(LEFT, deltaTime);
	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
		camera.ProcessKeyboard(RIGHT, deltaTime);
}

// glfw: whenever the window size changed (by OS or user resize) this callback function executes
// ---------------------------------------------------------------------------------------------
void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
	// make sure the viewport matches the new window dimensions; note that width and 
	// height will be significantly larger than specified on retina displays.
	glViewport(0, 0, width, height);
}


// glfw: whenever the mouse moves, this callback is called
// -------------------------------------------------------
void mouse_callback(GLFWwindow* window, double xpos, double ypos)
{
	if (firstMouse)
	{
		lastX = xpos;
		lastY = ypos;
		firstMouse = false;
	}

	float xoffset = xpos - lastX;
	float yoffset = lastY - ypos; // reversed since y-coordinates go from bottom to top

	lastX = xpos;
	lastY = ypos;

	camera.ProcessMouseMovement(xoffset, yoffset);
}

// glfw: whenever the mouse scroll wheel scrolls, this callback is called
// ----------------------------------------------------------------------
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
	camera.ProcessMouseScroll(yoffset);
}