#include <shader.h>
using namespace std;

Shader::Shader(const string& vertexShader, const string& fragmentShader) {
	GLuint vs = compileShader(GL_VERTEX_SHADER, readFile(vertexShader));
	GLuint fs = compileShader(GL_FRAGMENT_SHADER, readFile(fragmentShader));

	GLuint prog = glCreateProgram();
	glAttachShader(prog, vs);
	glAttachShader(prog, fs);
	glLinkProgram(prog);

	GLint ok;
	glGetProgramiv(prog, GL_LINK_STATUS, &ok);

	if (!ok) {
		char log[1024];
		glGetProgramInfoLog(prog, 1024, nullptr, log);
		cerr << "Shader link error:\n" << log << endl;
		exit(1);
	}

	glDeleteShader(vs);
	glDeleteShader(fs);

	shaderProgram = prog;
};


string Shader::readFile(const string& path) {

	ifstream file(path);
	if (!file.is_open()) {
		cerr << "Failed to open " << path << endl;
		exit(1);
	}
	return string((istreambuf_iterator<char>(file)),
		istreambuf_iterator<char>());
}

GLuint Shader::compileShader(GLenum type, const string& src) {
	GLuint shader = glCreateShader(type);
	const char* c = src.c_str();
	glShaderSource(shader, 1, &c, nullptr);
	glCompileShader(shader);

	GLint ok;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[1024];
		glGetShaderInfoLog(shader, 1024, nullptr, log);
		cerr << "Shader compile error:\n" << log << endl;
		exit(1);
	}
	return shader;
}
