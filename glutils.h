
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include <string>
static void (*uniFuncs[4])() = {
	(void (*)())glUniform1f, (void (*)())glUniform2f, (void (*)())glUniform3f, (void (*)())glUniform4f
};


template <typename ... ARGS> void setUniform(GLuint id, const std::string &name, ARGS&& ... args) {
	((void (*)(GLint, ARGS...))uniFuncs[sizeof...(ARGS)-1])(glGetUniformLocation(id, name.c_str()), std::forward<ARGS>(args)...);
}

void vertexAttribPointer(int id, const std::string &name, GLint size, GLenum type, GLboolean normalized, GLsizei stride, GLint offset) {
	GLuint h = glGetAttribLocation(id, name.c_str());
	glVertexAttribPointer(h, size, type, normalized, stride, reinterpret_cast<const GLvoid*>(offset));
	glEnableVertexAttribArray(h);
}

GLuint loadShader(GLenum shaderType, const std::string &source) {
	GLuint shader = glCreateShader(shaderType);
	if(shader) {
		const char *sources[1];
		sources[0] = source.c_str();
		glShaderSource(shader, 1, sources, NULL);
		glCompileShader(shader);
		GLint compiled = 0;
		glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
		if(!compiled) {
			GLint infoLen = 0;
			std::string msg;
			glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
			if(infoLen) {
				char buf[infoLen];
				glGetShaderInfoLog(shader, infoLen, NULL, buf);
				msg = buf;
				glDeleteShader(shader);
				shader = 0;
			}
			fprintf(stderr, "Shader Error: %s\n", msg.c_str());
		}
	} else
		fprintf(stderr, "Shader Error\n");

	return shader;
}


GLuint createProgram(const std::string &vertexSource, const std::string &fragmentSource) {
	GLuint vertexShader = loadShader(GL_VERTEX_SHADER, vertexSource);
	GLuint pixelShader = loadShader(GL_FRAGMENT_SHADER, fragmentSource);

	GLuint program = glCreateProgram();
	if(program) {
		glAttachShader(program, vertexShader);
		glAttachShader(program, pixelShader);
		glLinkProgram(program);
		GLint linkStatus = GL_FALSE;
		glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
		if(linkStatus != GL_TRUE) {
			GLint bufLength = 0;
			std::string msg;
			glGetProgramiv(program, GL_INFO_LOG_LENGTH, &bufLength);
			if(bufLength) {
				char buf[bufLength];
				glGetProgramInfoLog(program, bufLength, NULL, buf);
				msg = buf;
			}
			glDeleteProgram(program);
			fprintf(stderr, "Liker Error: %s\n", msg.c_str());
		}
	} else
		fprintf(stderr, "Liker Error\n");
	return program;
}

