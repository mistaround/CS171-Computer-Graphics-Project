#include <iostream>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include "Shader.h"
#include "App.h"

App::App(const int width, const int height, GLFWwindow* window) {
	width_ = width;
	height_ = height;
	window_ = window;
	camera_ = NULL;
	controls_ = NULL;
}

App::~App() {
	if (camera_)
		delete camera_;
	if (controls_)
		delete controls_;

	for (std::vector<Object*>::iterator obj = objects_.begin(); obj != objects_.end(); ++obj) {
		delete (*obj);
	}
	objects_.clear();
}

int App::getWindowWidth() {
	return width_;
}

int App::getWindowHeight() {
	return height_;
}

GLFWwindow* App::getWindow() {
	return window_;
}

Camera* App::getCamera() {
	return camera_;
}

bool App::loadObject(std::string path, std::string name, glm::vec3 pos, float scale) {
	Assimp::Importer importer;

	// Read file and store as a "scene"
	const aiScene* scene = importer.ReadFile(path + name, aiProcess_Triangulate |
		aiProcess_CalcTangentSpace |
		aiProcess_JoinIdenticalVertices);

	if (scene) {
		Material* mat;
		Object* obj;
		Mesh* mesh;

		// Create a materials from the loaded assimp materials
		for (unsigned int m = 0; m < scene->mNumMaterials; m++) {
			mat = new Material();
			mat->loadAssimpMaterial(scene->mMaterials[m], path);
			materials_[m] = mat;
		}

		// Create objects and add to objects_ vector. An object has a mesh, a material and some other properties.
		for (unsigned int m = 0; m < scene->mNumMeshes; m++) {
			// Create new object
			obj = new Object();

			// Create a mesh from the loaded assimp mesh
			mesh = new Mesh();
			mesh->loadAssimpMesh(scene->mMeshes[m]);
			// Asign the object this mesh.
			obj->mesh_ = mesh;

			// Store pointer to material used
			obj->material_ = materials_[scene->mMeshes[m]->mMaterialIndex];

			obj->setScale(scale);
			obj->setPosition(pos);
			objects_.push_back(obj);
		}
	}
	else {
		std::cerr << "Mesh: " << importer.GetErrorString() << std::endl;
		return false;
	}

	return true;
}

bool App::initialize() {
	std::cout << "Initializing VCT" << std::endl;

	// Init camera parameters
	glm::vec3 pos = glm::vec3(0.0, 0.0, 5.0);
	glm::vec3 up = glm::vec3(0.0, 1.0, 0.0);
	float yaw = -3.1415f / 2.0f;
	float pitch = 0.0f;

	// Camera position, camera direction, world up vector, field of view, aspect ratio, min distance, max distance
	camera_ = new Camera(pos, yaw, pitch, up, 45.0f, (float)width_ / height_, 0.1f, 1000.0f);

	// Speed, Mouse sensitivity
	controls_ = new Controls(10.0f, 0.0015f);

	standardShader_ = loadShaders("../shaders/render.vert", "../shaders/render.frag");
	shadowShader_ = loadShaders("../shaders/shadow.vert", "../shaders/shadow.frag");
	voxelizationShader_ = loadShaders("../shaders/Voxel.vert", "../shaders/Voxel.frag", "../shaders/Voxel.geom");
	renderVoxelsShader_ = loadShaders("../shaders/renderVoxels.vert", "../shaders/renderVoxels.frag", "../shaders/renderVoxels.geom");

	// Load objects
	std::cout << "Loading objects... " << std::endl;
	loadObject("../data/models/crytek-sponza/", "sponza.obj", glm::vec3(0.0f), sponzaScale_);
	//loadObject("../data/models/", "suzanne.obj");
	std::cout << "Loading done! " << objects_.size() << " objects loaded" << std::endl;

	// Sort object so opaque objects are rendered first
	std::sort(objects_.begin(), objects_.end(), compareObjects);

	// Create VAO for 3D texture. Won't really store any information but it's still needed.
	glGenVertexArrays(1, &texture3DVertexArray_);

	// ------------------------------------------------------------------- //
	// --------------------- Shadow map initialization ------------------- //
	// ------------------------------------------------------------------- //
	// Create framebuffer for shadow map
	glGenFramebuffers(1, &depthFramebuffer_);
	glBindFramebuffer(GL_FRAMEBUFFER, depthFramebuffer_);

	// Depth texture
	depthTexture_.width = depthTexture_.height = 4096;

	glm::mat4 viewMatrix = glm::lookAt(lightDirection_, glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
	glm::mat4 projectionMatrix = glm::ortho	<float>(-120, 120, -120, 120, -500, 500);
	depthViewProjectionMatrix_ = projectionMatrix * viewMatrix;

	glGenTextures(1, &depthTexture_.textureID);
	glBindTexture(GL_TEXTURE_2D, depthTexture_.textureID);
	//glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, depthTexture_.width, depthTexture_.height, 0, GL_RG, GL_FLOAT, NULL);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, depthTexture_.width, depthTexture_.height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	// glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	// glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); 
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_R_TO_TEXTURE);

	glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, depthTexture_.textureID, 0);
	// No color output
	glDrawBuffer(GL_NONE);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		std::cout << "Error creating framebuffer" << std::endl;
		return false;
	}

	// ------------------------------------------------------------------- //
	// --------------------- 3D texture initialization ------------------- //
	// ------------------------------------------------------------------- //

	voxelTexture_.size = voxelDimensions_;
	voxelNormal_.size = voxelDimensions_;

	glEnable(GL_TEXTURE_3D);
	// Generate Voxel Albedo Texture
	glGenTextures(1, &voxelTexture_.textureID);
	glBindTexture(GL_TEXTURE_3D, voxelTexture_.textureID);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	// Fill 3D texture with empty values
	int numVoxels = voxelTexture_.size * voxelTexture_.size * voxelTexture_.size;
	GLubyte* data = new GLubyte[numVoxels * 4];
	for (int i = 0; i < voxelTexture_.size; i++) {
		for (int j = 0; j < voxelTexture_.size; j++) {
			for (int k = 0; k < voxelTexture_.size; k++) {
				data[4 * (i + j * voxelTexture_.size + k * voxelTexture_.size * voxelTexture_.size)] = 0;
				data[4 * (i + j * voxelTexture_.size + k * voxelTexture_.size * voxelTexture_.size) + 1] = 0;
				data[4 * (i + j * voxelTexture_.size + k * voxelTexture_.size * voxelTexture_.size) + 2] = 0;
				data[4 * (i + j * voxelTexture_.size + k * voxelTexture_.size * voxelTexture_.size) + 3] = 0;
			}
		}
	}

	glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8, voxelTexture_.size, voxelTexture_.size, voxelTexture_.size, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
	glGenerateMipmap(GL_TEXTURE_3D);

	// Generate Voxel Normal Texture
	glGenTextures(1, &voxelNormal_.textureID);
	glBindTexture(GL_TEXTURE_3D, voxelNormal_.textureID);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8, voxelNormal_.size, voxelNormal_.size, voxelNormal_.size, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
	glGenerateMipmap(GL_TEXTURE_3D);

	delete[] data;

	// Create projection matrices used to project stuff onto each axis in the Voxel step
	float size = voxelGridWorldSize_;
	// left, right, bottom, top, zNear, zFar
	projectionMatrix = glm::ortho(-size * 0.5f, size*0.5f, -size * 0.5f, size*0.5f, size*0.5f, size*1.5f);
	projX_ = projectionMatrix * glm::lookAt(glm::vec3(size, 0, 0), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
	projY_ = projectionMatrix * glm::lookAt(glm::vec3(0, size, 0), glm::vec3(0, 0, 0), glm::vec3(0, 0, -1));
	projZ_ = projectionMatrix * glm::lookAt(glm::vec3(0, 0, size), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));

	// Draw depth for shadow mapping and voxelize scene once
	drawDepthTexture();
	voxelizeScene();

	return true;
}

void App::depthmap() {
	// Create framebuffer for shadow map
	glGenFramebuffers(1, &depthFramebuffer_);
	glBindFramebuffer(GL_FRAMEBUFFER, depthFramebuffer_);

	// Depth texture
	depthTexture_.width = depthTexture_.height = 4096;

	glm::mat4 viewMatrix = glm::lookAt(lightDirection_, glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
	glm::mat4 projectionMatrix = glm::ortho	<float>(-120, 120, -120, 120, -500, 500);
	depthViewProjectionMatrix_ = projectionMatrix * viewMatrix;

	glGenTextures(1, &depthTexture_.textureID);
	glBindTexture(GL_TEXTURE_2D, depthTexture_.textureID);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, depthTexture_.width, depthTexture_.height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	// glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	// glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); 
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_R_TO_TEXTURE);

	glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, depthTexture_.textureID, 0);
	// No color output
	glDrawBuffer(GL_NONE);

	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		std::cout << "Error creating framebuffer" << std::endl;
	}
	drawDepthTexture();
}

void App::update(float deltaTime) {
	controls_->updateFromInputs(this, deltaTime);
	camera_->update();
	updateInput();
}

void App::updateInput() {
	// This is a bit silly
	if (!press1_ && glfwGetKey(window_, GLFW_KEY_1) == GLFW_PRESS) {
		showDiffuse_ = !showDiffuse_;
		press1_ = true;
	}
	if (!press2_ && glfwGetKey(window_, GLFW_KEY_2) == GLFW_PRESS) {
		showIndirectDiffuse_ = !showIndirectDiffuse_;
		press2_ = true;
	}
	if (!press3_ && glfwGetKey(window_, GLFW_KEY_3) == GLFW_PRESS) {
		showIndirectSpecular_ = !showIndirectSpecular_;
		press3_ = true;
	}
	if (!press4_ && glfwGetKey(window_, GLFW_KEY_4) == GLFW_PRESS) {
		showAmbientOcculision_ = !showAmbientOcculision_;
		press4_ = true;
	}
	if (!press5_ && glfwGetKey(window_, GLFW_KEY_5) == GLFW_PRESS) {
		showSoftShadow_ = !showSoftShadow_;
		press5_ = true;
	}

	if (glfwGetKey(window_, GLFW_KEY_1) == GLFW_RELEASE) {
		press1_ = false;
	}
	if (glfwGetKey(window_, GLFW_KEY_2) == GLFW_RELEASE) {
		press2_ = false;
	}
	if (glfwGetKey(window_, GLFW_KEY_3) == GLFW_RELEASE) {
		press3_ = false;
	}
	if (glfwGetKey(window_, GLFW_KEY_4) == GLFW_RELEASE) {
		press4_ = false;
	}
	if (glfwGetKey(window_, GLFW_KEY_5) == GLFW_RELEASE) {
		press5_ = false;
	}

	// When light source changed we need to re-Voxel to calculate the 3D texture and shadowmap (Time comsuming)
	// TODO: Optimize
	if (lightDirection_[0] < 0.8f && glfwGetKey(window_, GLFW_KEY_LEFT) == GLFW_PRESS) {
		lightDirection_ = lightDirection_ + glm::vec3(0.02, 0, 0);
		//voxelizeScene();
		depthmap();
	}

	if (lightDirection_[0] > -0.8f && glfwGetKey(window_, GLFW_KEY_RIGHT) == GLFW_PRESS) {
		lightDirection_ = lightDirection_ - glm::vec3(0.02, 0, 0);
		//voxelizeScene();
		depthmap();
	}

	if (lightDirection_[2] < 0.8f && glfwGetKey(window_, GLFW_KEY_UP) == GLFW_PRESS) {
		lightDirection_ = lightDirection_ + glm::vec3(0, 0, 0.02);
		//voxelizeScene();
		depthmap();
	}

	if (lightDirection_[2] > -0.8f && glfwGetKey(window_, GLFW_KEY_DOWN) == GLFW_PRESS) {
		lightDirection_ = lightDirection_ - glm::vec3(0, 0, 0.02);
		//voxelizeScene();
		depthmap();
	}
}

void App::draw() {
	glEnable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);

	// Draw to the screen  
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, width_, height_);
	// Set clear color and clear
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glm::mat4 viewMatrix = camera_->getViewMatrix();
	glm::mat4 projectionMatrix = camera_->getProjectionMatrix();

	glUseProgram(standardShader_);

	glm::vec3 camPos = camera_->getPosition();
	glUniform3f(glGetUniformLocation(standardShader_, "CamPos"), camPos.x, camPos.y, camPos.z);
	glUniform3f(glGetUniformLocation(standardShader_, "LightDir"), lightDirection_.x, lightDirection_.y, lightDirection_.z);
	glUniform1f(glGetUniformLocation(standardShader_, "VoxelGridWorldSize"), voxelGridWorldSize_);
	glUniform1i(glGetUniformLocation(standardShader_, "VoxelDimensions"), voxelDimensions_);

	glUniform1f(glGetUniformLocation(standardShader_, "ShowDiffuse"), showDiffuse_);
	glUniform1f(glGetUniformLocation(standardShader_, "ShowIndirectDiffuse"), showIndirectDiffuse_);
	glUniform1f(glGetUniformLocation(standardShader_, "ShowIndirectSpecular"), showIndirectSpecular_);
	glUniform1f(glGetUniformLocation(standardShader_, "ShowAmbientOcculision"), showAmbientOcculision_);
	glUniform1f(glGetUniformLocation(standardShader_, "ShowSoftShadow"), showSoftShadow_);

	glActiveTexture(GL_TEXTURE0 + 5);
	glBindTexture(GL_TEXTURE_2D, depthTexture_.textureID);
	glUniform1i(glGetUniformLocation(standardShader_, "ShadowMap"), 5);

	glActiveTexture(GL_TEXTURE0 + 6);
	glBindTexture(GL_TEXTURE_3D, voxelTexture_.textureID);
	glUniform1i(glGetUniformLocation(standardShader_, "VoxelTexture"), 6);

	glActiveTexture(GL_TEXTURE0 + 7);
	glBindTexture(GL_TEXTURE_3D, voxelNormal_.textureID);
	glUniform1i(glGetUniformLocation(standardShader_, "VoxelNormal"), 7);

	for (std::vector<Object*>::iterator obj = objects_.begin(); obj != objects_.end(); ++obj) {
		(*obj)->draw(viewMatrix, projectionMatrix, depthViewProjectionMatrix_, standardShader_);
	}

	// Draw voxels for debugging (can't draw large voxel sets like 512^3)
	//drawVoxels();
}

void App::drawDepthTexture() {
	glEnable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);

	// Draw to depth frame buffer instead of screen
	glBindFramebuffer(GL_FRAMEBUFFER, depthFramebuffer_);
	// Set viewport of framebuffer size
	glViewport(0, 0, depthTexture_.width, depthTexture_.height);
	// Set clear color and clear
	glClearColor(0, 0, 0, 1);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	for (std::vector<Object*>::iterator obj = objects_.begin(); obj != objects_.end(); ++obj) {
		(*obj)->drawToDepth(depthViewProjectionMatrix_, shadowShader_);
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, width_, height_);
}

void App::voxelizeScene() {
	glDisable(GL_CULL_FACE);
	glDisable(GL_DEPTH_TEST);

	glViewport(0, 0, voxelTexture_.size, voxelTexture_.size);
	glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glUseProgram(voxelizationShader_);

	// Set uniforms
	glUniform1i(glGetUniformLocation(voxelizationShader_, "VoxelDim"), voxelDimensions_);
	glUniform1i(glGetUniformLocation(voxelizationShader_, "VoxelTexDim"), voxelTexture_.size);
	glUniform1i(glGetUniformLocation(voxelizationShader_, "WorldSize"), voxelGridWorldSize_);
	glUniformMatrix4fv(glGetUniformLocation(voxelizationShader_, "ProjX"), 1, GL_FALSE, &projX_[0][0]);
	glUniformMatrix4fv(glGetUniformLocation(voxelizationShader_, "ProjY"), 1, GL_FALSE, &projY_[0][0]);
	glUniformMatrix4fv(glGetUniformLocation(voxelizationShader_, "ProjZ"), 1, GL_FALSE, &projZ_[0][0]);

	// Bind depth texture
	glActiveTexture(GL_TEXTURE0 + 5);
	glBindTexture(GL_TEXTURE_2D, depthTexture_.textureID);
	glUniform1i(glGetUniformLocation(voxelizationShader_, "ShadowMap"), 5);

	// Write Voxels with color information into a 3D-texture
	glBindImageTexture(6, voxelTexture_.textureID, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA8);
	glUniform1i(glGetUniformLocation(voxelizationShader_, "VoxelTexture"), 6);

	// Write Voxels with normal information into a 3D-texture
	glBindImageTexture(7, voxelNormal_.textureID, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA8);
	glUniform1i(glGetUniformLocation(voxelizationShader_, "VoxelNormal"), 7);

	for (std::vector<Object*>::iterator obj = objects_.begin(); obj != objects_.end(); ++obj) {
		(*obj)->drawTo3DTexture(voxelizationShader_, depthViewProjectionMatrix_);
	}

	glActiveTexture(GL_TEXTURE6);
	glBindTexture(GL_TEXTURE_3D, voxelTexture_.textureID);
	glGenerateMipmap(GL_TEXTURE_3D);

	glActiveTexture(GL_TEXTURE8);
	glBindTexture(GL_TEXTURE_3D, voxelNormal_.textureID);
	glGenerateMipmap(GL_TEXTURE_3D);
	// Reset viewport
	glViewport(0, 0, width_, height_);
}

void App::generateOctree()
{
	// TODO: Generate Octree structure using 3D Texture
}

void App::Aniso()
{
	// TODO: After octree, do anisotropic filtering
}

// For debugging
void App::drawVoxels() {
	glUseProgram(renderVoxelsShader_);

	int numVoxels = voxelTexture_.size * voxelTexture_.size * voxelTexture_.size;
	float voxelSize = voxelGridWorldSize_ / voxelTexture_.size;
	glUniform1i(glGetUniformLocation(renderVoxelsShader_, "Dimensions"), voxelTexture_.size);
	glUniform1i(glGetUniformLocation(renderVoxelsShader_, "TotalNumVoxels"), numVoxels);
	glUniform1f(glGetUniformLocation(renderVoxelsShader_, "VoxelSize"), voxelSize);
	glm::mat4 modelMatrix = glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(voxelSize)), glm::vec3(0, 0, 0));
	glm::mat4 viewMatrix = camera_->getViewMatrix();
	glm::mat4 modelViewMatrix = viewMatrix * modelMatrix;
	glm::mat4 projectionMatrix = camera_->getProjectionMatrix();
	glUniformMatrix4fv(glGetUniformLocation(renderVoxelsShader_, "ModelViewMatrix"), 1, GL_FALSE, &modelViewMatrix[0][0]);
	glUniformMatrix4fv(glGetUniformLocation(renderVoxelsShader_, "ProjectionMatrix"), 1, GL_FALSE, &projectionMatrix[0][0]);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_3D, voxelTexture_.textureID);
	glUniform1i(glGetUniformLocation(renderVoxelsShader_, "VoxelsTexture"), 0);

	glBindVertexArray(texture3DVertexArray_);
	glDrawArrays(GL_POINTS, 0, numVoxels);

	glBindVertexArray(0);
	glUseProgram(0);
}
