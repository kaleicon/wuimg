#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

void msg_callback(GLenum source, GLenum type, GLuint id, GLenum severity,
		GLsizei length, const GLchar* msg, const void* user_param) {
	(void) length;
	(void) user_param;
	fprintf(stderr, "GL %x callback: type = %x, id = %u, severity = %x, "
			"msg = %s\n",
		source, type, id, severity, msg);
}

GLuint link_program(GLuint vs, GLuint fs) {
	GLuint program;
	GLint status = GL_FALSE;

	program = glCreateProgram();
	glAttachShader(program, vs);
	glAttachShader(program, fs);
	glLinkProgram(program);
	glGetProgramiv(program, GL_LINK_STATUS, &status);
	if (status != GL_TRUE) {
		char logbuf[256] = {0};
		glGetProgramInfoLog(program, 512, NULL, logbuf);
		puts(logbuf);
		exit(1);
	}
	return program;
}

GLuint compile_shader(const char *path, GLenum type) {
	GLuint shader;
	GLint status = GL_FALSE;
	struct stat st;
	size_t filesize;
	char *source;
	int length[1];

	if (stat(path, &st) == -1) {
		perror("stat()");
		exit(1);
	}
	filesize = (size_t) st.st_size;
	source = malloc(filesize);
	if (source == NULL) {
		perror("malloc()");
		exit(1);
	}
	FILE* shaderfile = fopen(path, "r");
	fread(source, 1, filesize, shaderfile);
	fclose(shaderfile);
	length[0] = (const int) filesize;

	shader = glCreateShader(type);
	glShaderSource(shader, 1, (const char * const*)&source, length);
	puts(source);
	glCompileShader(shader);
	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	if (status != GL_TRUE) {
		char logbuf[256] = {0};
		glGetShaderInfoLog(shader, 256, NULL, logbuf);
		puts(logbuf);
		exit(1);
	}
	free(source);
	return shader;
}

GLuint init_gl() {
	GLuint vertex_buf, vs, fs, program;
	const char *vs_file = "vertex.glsl";
	const char *fs_file = "fragment.glsl";
	float triangle[] = {0.0, 0.2f, -0.5, -0.5, 0.5, -0.5};

	glEnable(GL_DEBUG_OUTPUT);
	glDebugMessageCallback(msg_callback, 0);

	glGenBuffers(1, &vertex_buf);
	glBindBuffer(GL_ARRAY_BUFFER, vertex_buf);
	glBufferData(GL_ARRAY_BUFFER, sizeof(triangle), triangle, GL_STATIC_DRAW);

	vs = compile_shader(vs_file, GL_VERTEX_SHADER);
	fs = compile_shader(fs_file, GL_FRAGMENT_SHADER);
	program = link_program(vs, fs);
	glDetachShader(program, vs);
	glDetachShader(program, fs);
	return program;
}

void key_callback (GLFWwindow* window, int key, int scancode, int action, int mods) {
	(void)scancode;
	(void)mods;
	if (action == GLFW_PRESS || action == GLFW_REPEAT) {
		switch (key) {
		case GLFW_KEY_ESCAPE:
		case GLFW_KEY_Q:
			glfwSetWindowShouldClose(window, GLFW_TRUE);
			break;
		case GLFW_KEY_LEFT:
			puts("Left.");
			break;
		case GLFW_KEY_RIGHT:
			puts("Right.");
			break;
		}
	}
}

void window_loop(GLFWwindow* window, GLuint program) {
	while (!glfwWindowShouldClose(window)) {
		glClearColor(0.2f, 0.2f, 0.2f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT);

		glUseProgram(program);
		glDrawArrays(GL_TRIANGLES, 0, 3);
/*		glDisableVertexAttribArray(0);
		glUseProgram(0);
*/
		glfwSwapBuffers(window);
		glfwPollEvents();
	}
}

GLFWwindow* init_window(int width, int height) {
	GLFWwindow* window;
	if (!glfwInit()) {
		puts("Couldn't start glfw.");
		exit(1);
	}
	window = glfwCreateWindow(width, height, "Aayy", NULL, NULL);
	if (!window) {
		glfwTerminate();
		puts("Couldn't create glfw window.");
		exit(1);
	}
	glfwSetKeyCallback(window, key_callback);
	glfwMakeContextCurrent(window);
	glfwSwapInterval(1);
	return window;
}

void setup(int width, int height) {
	GLuint program;
	GLuint position;
	GLFWwindow* window = init_window(width, height);
	GLenum glew_status = glewInit();
	if (glew_status != GLEW_OK) {
		printf("Error %d when trying to init GLEW\n", glew_status);
		exit(1);
	}

	program = init_gl();
	position = (GLuint)glGetAttribLocation(program, "position");
	glEnableVertexAttribArray(position);
	glVertexAttribPointer(position, 2, GL_FLOAT, GL_FALSE, 0, 0);

	window_loop(window, program);

	glDeleteProgram(program);
	glfwDestroyWindow(window);
	glfwTerminate();
}

int main(int argc, char *argv[]) {
	int width = 400;
	int height = 400;
	if (argc == 4 && !strncmp(argv[1], "-d", 2)) {
		width = atoi(argv[2]);
		height = atoi(argv[3]);
	}
	setup(width, height);
	return 0;
}
