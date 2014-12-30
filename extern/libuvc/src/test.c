/*********************************************************************
* Software License Agreement (BSD License)
*
*  Copyright (C) 2010-2012 Ken Tossell
*  All rights reserved.
*
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions
*  are met:
*
*   * Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*   * Redistributions in binary form must reproduce the above
*     copyright notice, this list of conditions and the following
*     disclaimer in the documentation and/or other materials provided
*     with the distribution.
*   * Neither the name of the author nor other contributors may be
*     used to endorse or promote products derived from this software
*     without specific prior written permission.
*
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
*  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
*  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
*  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
*  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
*  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
*  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
*  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
*  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
*  POSSIBILITY OF SUCH DAMAGE.
*********************************************************************/
#include <stdio.h>
#include <pthread.h>

#include <GL/glut.h>
#include <GL/gl.h>

#include "libuvc/libuvc.h"

GLuint texID = 0;
uvc_frame_t *new_rgb_frame = NULL;

pthread_cond_t  frame_cnd;
pthread_mutex_t frame_mtx;

void frame_callback(uvc_frame_t *frame, void *ptr) {
  uvc_frame_t *rgb;
  uvc_error_t ret;

#if 0
  printf("callback! length = %u, ptr = %p\n",
		(unsigned int)frame->data_bytes,
		ptr );
#endif

  rgb = uvc_allocate_frame(frame->width * frame->height * 3);
  if (!rgb) {
    printf("unable to allocate rgb frame!");
    return;
  }

  ret = uvc_mjpeg2rgb(frame, rgb);
  if (ret) {
    uvc_free_frame(rgb);
    uvc_perror(ret, "uvc_any2rgb");
    return;
  }

  pthread_mutex_lock(&frame_mtx);
  new_rgb_frame = rgb;
  pthread_cond_broadcast(&frame_cnd);
  pthread_mutex_unlock(&frame_mtx);
}

void idle(void)
{
  if( pthread_mutex_lock(&frame_mtx) ) {
    return;
  }
  if( pthread_cond_wait(&frame_cnd, &frame_mtx) ) {
      pthread_mutex_unlock(&frame_mtx);
      return;
  }

  if( new_rgb_frame ) {

          if( !texID ) {
            glGenTextures(1, &texID);
            glBindTexture(GL_TEXTURE_2D, texID);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

            printf("generated texID=%d\n", texID);
          }
          else {
            glBindTexture(GL_TEXTURE_2D, texID);
          }

            glTexImage2D(
                    GL_TEXTURE_2D, 0,
                    GL_RGB, new_rgb_frame->width, new_rgb_frame->height, 0,
                    GL_RGB, GL_UNSIGNED_BYTE,
                    new_rgb_frame->data );

            uvc_free_frame(new_rgb_frame);
            glBindTexture(GL_TEXTURE_2D, 0);

            new_rgb_frame = NULL;

            glutPostRedisplay();
  }
      pthread_mutex_unlock(&frame_mtx);
}

GLfloat const quad[] = {
  0,0,
  1,0,
  1,1,
  0,1
};

void display(void)
{
  int const width  = glutGet(GLUT_WINDOW_WIDTH);
  int const height = glutGet(GLUT_WINDOW_HEIGHT);
  float const aspect = (float)width/(float)height;

  glViewport(0,0, width, height);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(0, 1, 1, 0, -1, 1);

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  glEnableClientState(GL_VERTEX_ARRAY);
  glEnableClientState(GL_TEXTURE_COORD_ARRAY);
  glVertexPointer(2, GL_FLOAT, 0, quad);
  glTexCoordPointer(2, GL_FLOAT, 0, quad);

  if( texID ) {
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texID);
    glDrawArrays(GL_QUADS, 0, 4);
  }
  else {
    glClearColor(0, 0, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
  }

  glutSwapBuffers();
}

int main(int argc, char **argv) {
  uvc_context_t *ctx;
  uvc_error_t res;
  uvc_device_t *dev;
	uvc_device_t **device_list;
  uvc_device_handle_t *devh;
  uvc_stream_ctrl_t ctrl;

  if( pthread_mutex_init(&frame_mtx, NULL) 
   || pthread_cond_init(&frame_cnd, NULL) ) {
    perror("mutex / cond init");
    return -1;
  }

  res = uvc_init(&ctx, NULL);

  if (res < 0) {
    uvc_perror(res, "uvc_init");
    return res;
  }

  puts("UVC initialized");

#if 0
  res = uvc_find_device(
      ctx, &dev,
      0, 0, NULL);
#endif

	unsigned int i_device = 0;
	if( 1 < argc ) {
		i_device = strtol(argv[argc-1], NULL, 0);
	}

	res = uvc_get_device_list(ctx, &device_list);
	if( 0 > res ) {
		uvc_perror(res, "uvc_get_device_list");
		return res;
	}
	if( !device_list ) {
		return -1;
	}
	
	unsigned int n_devices = 0;
	uvc_device_t **dev_i;
	for( dev_i = device_list; *dev_i; dev_i++ ) {
		uvc_device_descriptor_t *dev_desc;
		res = uvc_get_device_descriptor(*dev_i, &dev_desc);
		printf("%u: vendor='%s' product='%s' serial='%s'\n",
			n_devices,
			dev_desc->manufacturer,
			dev_desc->product,
			dev_desc->serialNumber );
		uvc_free_device_descriptor(dev_desc);

		++n_devices;
	}

	if( !n_devices
	 || i_device > n_devices
	 || 0 > i_device ) {
		return -2;
	}

	dev = device_list[i_device];

  if (res < 0) {
    uvc_perror(res, "uvc_find_device");
  } else {
    puts("Device found");

    res = uvc_open(dev, &devh);

    if (res < 0) {
      uvc_perror(res, "uvc_open");
    } else {
      puts("Device opened");
    
        glutInit(&argc, argv);
        glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE);

        glutCreateWindow("libuvc test");
        glutDisplayFunc(display);
        glutIdleFunc(idle);

      uvc_print_diag(devh, stderr);

      res = uvc_get_stream_ctrl_format_size(
          devh, &ctrl, UVC_FRAME_FORMAT_MJPEG,
          // 1280, 720,
          1280, 1024,
          // 640, 480,
          // 800, 600,
          0 //30
      );

      uvc_print_stream_ctrl(&ctrl, stderr);

      if (res < 0) {
        uvc_perror(res, "get_mode");
      } else {
        res = uvc_start_streaming(devh, &ctrl, frame_callback, (void*)12345, 0);

        if (res < 0) {
          uvc_perror(res, "start_streaming");
        } else {
          // res= uvc_set_ae_mode(devh, 2 /* = UVC_AUTO_EXPOSURE_MODE_AUTO */);
          uvc_perror(res, "set_ae_mode");

          glutMainLoop();

          uvc_stop_streaming(devh);
	  puts("Done streaming.");
        }
      }

      uvc_close(devh);
      puts("Device closed");
    }

    uvc_unref_device(dev);
  }

  uvc_exit(ctx);
  puts("UVC exited");

  return 0;
}

