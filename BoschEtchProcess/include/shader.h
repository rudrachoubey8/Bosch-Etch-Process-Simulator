#pragma once
#include <string>
#include <fstream>
#include <iostream>
#include <stdlib.h>
#include <glad/glad.h>


class Shader {

public:
	GLuint shaderProgram;
	Shader(const std::string& vertexShader, const std::string& fragmentShader) ;
private:
	std::string readFile(const std::string& filename);
	GLuint compileShader(GLenum type, const std::string& src);

};