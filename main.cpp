/* Raspberry PI GLES example by Jonas Minnberg (sasq64@gmail.com)
 * PUBLIC DOMAIN
 */
#include <linux/input.h>
#include <bcm_host.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>

#include <cstdio>
#include <cstdint>
#include <unordered_map>
#include <deque>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include "stb_easy_font.h"
#include "glutils.h"

using namespace std;

class display_exception : public exception {
public:
	display_exception(const string &msg) : msg(msg) {}
	virtual const char *what() const throw() { return msg.c_str(); }
private:
	string msg;
};


void print_string(float x, float y, char *text, float r, float g, float b)
{
}



static bool test_bit(const vector<uint8_t> &v, int n) {
	return (v[n/8] & (1<<(n%8))) != 0;
}

static vector<string> listFiles(const string &dirName) {
	DIR *dir;
	struct dirent *ent;
	vector<string> rc;
	if((dir = opendir(dirName.c_str())) != nullptr) {
		while ((ent = readdir (dir)) != nullptr) {
			char *p = ent->d_name;
			if(p[0] == '.' && (p[1] == 0 || (p[1] == '.' && p[2] == 0)))
				continue;
			rc.emplace_back(dirName + "/" + ent->d_name);
		}
		closedir(dir);
	}
	return rc;
}

static EGL_DISPMANX_WINDOW_T nativeWindow;
static EGLConfig eglConfig;
static EGLContext eglContext;
static EGLDisplay eglDisplay;
static EGLSurface eglSurface;

static mutex keyMutex;
static uint8_t pressed_keys[512];
static deque<int> key_events;

int getKey() {
	lock_guard<mutex> guard(keyMutex);
	int k = -1;
	if(key_events.size() > 0) {
		k = key_events.front();
		key_events.pop_front();
	}
	return k;
}

void initKeyboard() {

	memset(pressed_keys, 0, sizeof(pressed_keys));

	thread keyboardThread = thread([=]() {

		vector<uint8_t> evbit((EV_MAX+7)/8);
		vector<uint8_t> keybit((KEY_MAX+7)/8);

		// Find all input devices generating keys we are interested in
		int fd = -1;
		vector<int> fdv;
		for(auto f : listFiles("/dev/input")) {
			fd = ::open(f.c_str(), O_RDONLY, 0);
			if(fd >= 0) {
				ioctl(fd, EVIOCGBIT(0, evbit.size()), &evbit[0]);
				if(test_bit(evbit, EV_KEY)) {
					ioctl(fd, EVIOCGBIT(EV_KEY, keybit.size()), &keybit[0]);
					if(test_bit(keybit, KEY_LEFT) || test_bit(keybit, BTN_LEFT)) {
						ioctl(fd, EVIOCGRAB, 1);
						fdv.push_back(fd);
						continue;
					}
				}
				close(fd);
			}
		}

		if(fdv.size() == 0)
			return;

		int maxfd = -1;
		fd_set readset;
		struct timeval tv;

		vector<uint8_t> buf(256);

		// Input loop; Watch file handles for input events and put them on a queue
		while(true) {
			FD_ZERO(&readset);
			for(auto fd : fdv) {
				FD_SET(fd, &readset);
				if(fd > maxfd)
					maxfd = fd;
			}
			tv.tv_sec = 1;
			tv.tv_usec = 0;
			if(select(maxfd+1, &readset, nullptr, nullptr, &tv) <= 0)
				continue;
			for(auto fd : fdv) {
				if(FD_ISSET(fd, &readset)) {
					int rc = read(fd, &buf[0], sizeof(struct input_event) * 4);
					auto *ptr = (struct input_event*)&buf[0];
					while(rc >= sizeof(struct input_event)) {
						if(ptr->type == EV_KEY) {
							lock_guard<mutex> guard(keyMutex);
							if(ptr->value) {
								key_events.push_back(ptr->code);
							}
							if(ptr->code < 512)
								pressed_keys[ptr->code] = ptr->value;
						}
						ptr++;
						rc -= sizeof(struct input_event);
					}
				}
			}
		}
	});
	keyboardThread.detach();
}

void initBroadcom() {

	bcm_host_init();

	DISPMANX_ELEMENT_HANDLE_T dispman_element;
	DISPMANX_DISPLAY_HANDLE_T dispman_display;
	DISPMANX_UPDATE_HANDLE_T dispman_update;
	VC_RECT_T dst_rect;
	VC_RECT_T src_rect;

	uint32_t display_width;
	uint32_t display_height;

	// create an EGL window surface, passing context width/height
	if(graphics_get_display_size(0 /* LCD */, &display_width, &display_height) < 0)
		throw display_exception("Cound not get display size");

	dst_rect.x = 0;
	dst_rect.y = 0;
	dst_rect.width = display_width;
	dst_rect.height = display_height;

	uint16_t dwa = 0;
	uint16_t dha = 0;

	// Scale 50% on hires screens
	if(display_width > 1280) {
		display_width /= 2;
		display_height /= 2;
		dwa = display_width;
		dha = display_height;
	}

	src_rect.x = 0;
	src_rect.y = 0;
	src_rect.width = display_width << 16 | dwa;
	src_rect.height = display_height << 16 | dha;

	dispman_display = vc_dispmanx_display_open(0 /* LCD */);
	dispman_update = vc_dispmanx_update_start(0);

	dispman_element = vc_dispmanx_element_add(dispman_update,
		dispman_display, 0, &dst_rect, 0, &src_rect,
		DISPMANX_PROTECTION_NONE, nullptr, nullptr, DISPMANX_NO_ROTATE);

	nativeWindow.element = dispman_element;
	nativeWindow.width = display_width;
	nativeWindow.height = display_height;
	vc_dispmanx_update_submit_sync(dispman_update);
}

void initEGL() {

	EGLint numConfigs;
	EGLConfig config;
	EGLConfig configList[32];

	eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);

	EGLint m0;
	EGLint m1;

	eglInitialize(eglDisplay, &m0, &m1);
	eglGetConfigs(eglDisplay, configList, 32, &numConfigs);

	for(int i=0; i<numConfigs; i++) {
		EGLint conf, stype, caveat, sbuffers;
		eglGetConfigAttrib(eglDisplay, configList[i], EGL_CONFORMANT, &conf);
		eglGetConfigAttrib(eglDisplay, configList[i], EGL_SURFACE_TYPE, &stype);
		eglGetConfigAttrib(eglDisplay, configList[i], EGL_CONFIG_CAVEAT, &caveat);
		eglGetConfigAttrib(eglDisplay, configList[i], EGL_SAMPLE_BUFFERS, &sbuffers);

		// Pick a ES 2 context that preferably has some AA
		if((conf & EGL_OPENGL_ES2_BIT) && (stype & EGL_WINDOW_BIT)) {
			config = configList[i];
			if(sbuffers > 0) {
				break;
			}
		}
	}

	if(config == nullptr)
		throw display_exception("Could not find compatible config");

	const EGLint attribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE, EGL_NONE
	};

	eglContext = eglCreateContext(eglDisplay, config, nullptr, attribs);
	if(eglContext == EGL_NO_CONTEXT)
		throw display_exception("Cound not create GL context");

	eglSurface = eglCreateWindowSurface(eglDisplay, config, &nativeWindow, nullptr);
	if(eglSurface == EGL_NO_SURFACE)
		throw display_exception("Cound not create GL surface");

	if(eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext) == EGL_FALSE)
		throw display_exception("Cound not set context");

	eglConfig = config;
}
static int recBuf = -1;
GLuint program;

void initCube() {
	static vector<float> p {
		-1, 1, 0, 0,
		1, 1, 1, 0,
		-1, -1, 0, 1,
		1, -1, 1, 1,
		0,0,1,0,0,1,1,1
	};

	glGenBuffers(1, (GLuint*)&recBuf);
	glBindBuffer(GL_ARRAY_BUFFER, recBuf);
	glBufferData(GL_ARRAY_BUFFER, p.size() * 4, &p[0], GL_STATIC_DRAW);

	program = createProgram(R"(
		precision mediump float;
		attribute vec4 vertex;
		void main() {
		  gl_Position = vertex;
		}
	)",R"(
		uniform vec4 color;
		void main() {
		  gl_FragColor = color;
		}
	)");
}

void drawCube() {	
	glBindBuffer(GL_ARRAY_BUFFER, recBuf);
	setUniform(program, "color", 0.5, 0.3, 0.9, 1.0);
	vertexAttribPointer(program, "vertex", 2, GL_FLOAT, GL_FALSE, 16, 0);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

int main() {

	initKeyboard();
	initBroadcom();
	initEGL();

	initCube();

	glClearColor(1.0, 0.0, 1.0, 1.0);

	while(true) {
		glClear(GL_COLOR_BUFFER_BIT);
		//print_string(10, 10, "Hello", 255, 255, 255);
		int key = getKey();
		if(key == KEY_ESC || key == KEY_SPACE)
			break;
		eglSwapBuffers(eglDisplay, eglSurface);
	}

	return 0;
};
