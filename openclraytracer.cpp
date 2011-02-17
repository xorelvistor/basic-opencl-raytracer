#include <windows.h>
#include <gl/glew.h>
#include <gl/glut.h>
#include <cl/cl.h>
#include <cl/cl_gl.h>
#include <cstdio>
#include <cassert>
#include <vector>

#define SCRWIDTH 640
#define SCRHEIGHT 480

cl_context context = 0;
cl_device_id device = 0;
cl_command_queue queue = 0;
cl_mem tex_cl = 0;
cl_kernel kernel = 0;
GLuint texture = 0;
std::vector<cl_program> progs;
std::vector<cl_kernel> kerns;

size_t shrRoundUp(int group_size, int global_size) {
	int r = global_size % group_size;

	return r == 0
		? global_size
		: global_size + group_size - r;
}

void startKernel(int w, int h) {
	const size_t local_ws[] = {8, 16};
	const size_t global_ws[] = {shrRoundUp(local_ws[0], w), shrRoundUp(local_ws[1], h)};

	cl_int error = clEnqueueAcquireGLObjects(queue, 1, &tex_cl, 0, NULL, NULL); assert(!error);
	error = clEnqueueNDRangeKernel(queue, kernel, 2, NULL, global_ws, local_ws, 0, NULL, NULL); assert(!error);
	error = clEnqueueReleaseGLObjects(queue, 1, &tex_cl, 0, NULL, NULL); assert(!error);
	error = clFinish(queue); assert(!error);
}

void render(void) {
	startKernel(SCRWIDTH, SCRHEIGHT);
	glClear(GL_COLOR_BUFFER_BIT);
	glRasterPos2i(0, 0);
	glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, texture);
	glDrawPixels(SCRWIDTH, SCRHEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, 0);
	glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0);
	glutSwapBuffers();
	glutPostRedisplay();
}

void reshape(int x, int y) {
	glViewport(0, 0, x, y);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0.0, 1.0, 0.0, 1.0, 0.0, 1.0); 
}

void clInit() {
	cl_uint platforms;
	cl_platform_id platform;

	cl_int error = clGetPlatformIDs(1, &platform, &platforms); assert(!error);
	error = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 1, &device, NULL); assert(!error);

	cl_context_properties props[] = {
		CL_GL_CONTEXT_KHR,   (cl_context_properties) wglGetCurrentContext(),
		CL_WGL_HDC_KHR,      (cl_context_properties) wglGetCurrentDC(),
		CL_CONTEXT_PLATFORM, (cl_context_properties) platform, 
		0
	};

	context = clCreateContext(props, 1, &device, NULL, NULL, &error); assert(!error);
	queue = clCreateCommandQueue(context, device, 0, &error); assert(!error);

	cl_bool imageSupport;
	clGetDeviceInfo(device, CL_DEVICE_IMAGE_SUPPORT, sizeof(imageSupport), &imageSupport, NULL);
	printf("%s...\n\n", 
		imageSupport 
		? "[CL] Texture support available" 
		: "[CL] Texture support NOT available, this application will not be able to run.");
}

char* readfile(const char *filename, unsigned *size) {
	FILE *f = fopen(filename, "rb"); assert(f);

	fseek(f, 0, SEEK_END);
	*size = ftell(f);
	fseek(f, 0, SEEK_SET);

	char *buf = new char[*size + 1];
	size_t read = fread(buf, 1, *size, f);
	assert(read == *size);
	buf[read] = 0;

	return buf;
}

void clPrintBuildLog(cl_program prog) {
	size_t size;
	clGetProgramBuildInfo(prog, device, CL_PROGRAM_BUILD_LOG, 0, NULL, &size);

	char *txt = new char[size+1];
	clGetProgramBuildInfo(prog, device, CL_PROGRAM_BUILD_LOG, size, txt, NULL);
	txt[size] = '\0';
	printf("%s", txt);

#if _DEBUG
	OutputDebugStringA(txt);
	__asm int 3;
#endif
	delete[] txt;
}

cl_kernel loadAndBuildKernel(const char *filename, const char *entrypoint){
	unsigned size;
	const char *file = readfile(filename, &size);

	cl_int error;
	cl_program prog = clCreateProgramWithSource(context, 1, &file, &size, &error); assert(!error);
	cl_int berr = clBuildProgram(prog, 1, &device, "-Werror -cl-strict-aliasing -cl-unsafe-math-optimizations -cl-finite-math-only", NULL, NULL);
	if(berr != CL_SUCCESS) clPrintBuildLog(prog);

	delete[] file;
	cl_kernel k = clCreateKernel(prog, entrypoint, &error); assert(!error);

	progs.push_back(prog);
	kerns.push_back(k);

	return k;
}

void appInit(int w, int h) {
	glGenBuffersARB(1, &texture);
	glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, texture);
	glBufferDataARB(GL_PIXEL_UNPACK_BUFFER_ARB, w * h * sizeof(GLubyte) * 4, 0, GL_STREAM_DRAW_ARB);
	glBindBufferARB(GL_PIXEL_UNPACK_BUFFER_ARB, 0);

	cl_int error;
	tex_cl = clCreateFromGLBuffer(context, CL_MEM_WRITE_ONLY, texture, &error); assert(!error);

	kernel = loadAndBuildKernel("data/kernel.c", "raytracer");

	error = clSetKernelArg(kernel, 0, sizeof(cl_mem), &tex_cl); assert(!error);
	error = clSetKernelArg(kernel, 1, sizeof(cl_uint), &w); assert(!error);
	error = clSetKernelArg(kernel, 2, sizeof(cl_uint), &h); assert(!error);
}

void idle() { glutPostRedisplay(); }

void cleanup(){
	for(size_t i = 0; i < progs.size(); i++) clReleaseProgram(progs[i]);
	for(size_t i = 0; i < kerns.size(); i++) clReleaseKernel(kerns[i]);
	glDeleteBuffersARB(1, &texture);
	clReleaseMemObject(tex_cl);
	clReleaseCommandQueue(queue);
	clReleaseContext(context);
}

int main(int argc, char **argv) {
	atexit(cleanup);

	glutInit(&argc, argv);

	glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB);
	glutInitWindowPosition(100, 100);
	glutInitWindowSize(SCRWIDTH, SCRHEIGHT);
	glutCreateWindow("OpenCL raytracer");

	glewInit();
	clInit();
	appInit(SCRWIDTH, SCRHEIGHT);

	glutDisplayFunc(render);
	glutIdleFunc(idle);
	glutReshapeFunc(reshape);

	glutMainLoop();

	return 0;
}
