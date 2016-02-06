#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <jpeglib.h>
#include <getopt.h>
#define GL_GLEXT_PROTOTYPES
#include <GLFW/glfw3.h>
#include "jpeg_wrap.h"

#define NAME "test_libjpeg"

static const char YUV_VERT[]="\
#version 140\n\
in vec3 in_pos;\n\
in ivec2 in_tex;\n\
out vec2 out_tex;\n\
void main() {\n\
  gl_Position = vec4(in_pos.x, in_pos.y, in_pos.z, 1.0);\n\
  out_tex = vec2(in_tex);\n\
}";

static const char YUV_FRAG[]="\
#version 140\n\
in vec2 out_tex;\n\
out vec4 color;\n\
uniform usampler2D y_tex;\n\
uniform usampler2D u_tex;\n\
uniform usampler2D v_tex;\n\
void main() {\n\
  int s=int(out_tex.s);\n\
  int t=int(out_tex.t);\n\
  float y=float(texelFetch(y_tex,ivec2(s,t),0).r);\n\
  float u=float(texelFetch(u_tex,ivec2(s>>1,t>>1),0).r);\n\
  float v=float(texelFetch(v_tex,ivec2(s>>1,t>>1),0).r);\n\
  float r=y+1.402*(v-128);\n\
  float g=y-0.34414*(u-128)-0.71414*(v-128);\n\
  float b=y+1.772*(u-128);\n\
  color=vec4(r/255.0,g/255.0,b/255.0,1.0);\n\
}";

typedef struct vertex vertex;

struct vertex {
  GLfloat x, y, z;
  GLint s, t;
};

int od_ilog(uint32_t _v) {
  /*On a Pentium M, this branchless version tested as the fastest on
     1,000,000,000 random 32-bit integers, edging out a similar version with
     branches, and a 256-entry LUT version.*/
  int ret;
  int m;
  ret = !!_v;
  m = !!(_v&0xFFFF0000)<<4;
  _v >>= m;
  ret |= m;
  m = !!(_v&0xFF00)<<3;
  _v >>= m;
  ret |= m;
  m = !!(_v&0xF0)<<2;
  _v >>= m;
  ret |= m;
  m = !!(_v&0xC)<<1;
  _v >>= m;
  ret |= m;
  ret += !!(_v&0x2);
  return ret;
}

#define OD_ILOG(x) (od_ilog(x))

static void error_callback(int error, const char* description) {
  fprintf(stderr, "glfw error %i: %s\n", error, description);
}

static void key_callback(GLFWwindow* window, int key, int scancode, int action,
 int mods) {
  (void)scancode;
  (void)mods;
  if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
    glfwSetWindowShouldClose(window, GL_TRUE);
  }
}

/* Compile the shader fragment. */
static GLint load_shader(GLuint *_shad,GLenum _shader,const char *_src) {
  int    len;
  GLuint shad;
  char info[8192];
  GLint  status;
  len = strlen(_src);
  shad = glCreateShader(_shader);
  glShaderSource(shad, 1, &_src, &len);
  glCompileShader(shad);
  glGetShaderInfoLog(shad, 8192, &len, info);
  if (len > 0) {
    printf("%s", info);
  }
  glGetShaderiv(shad, GL_COMPILE_STATUS, &status);
  if (status != GL_TRUE) {
    printf("Failed to compile fragment shader.\n");
    return GL_FALSE;
  }
  *_shad = shad;
  return GL_TRUE;
}

static GLint setup_shader(GLuint *_prog,const char *_vert,const char *_frag) {
  GLuint prog;
  int len;
  char info[8192];
  GLint  status;
  prog = glCreateProgram();
  if (_vert!=NULL) {
    GLuint vert;
    if (!load_shader(&vert, GL_VERTEX_SHADER, _vert)) {
      return GL_FALSE;
    }
    glAttachShader(prog, vert);
  }
  if (_frag!=NULL) {
    GLuint frag;
    if (!load_shader(&frag, GL_FRAGMENT_SHADER, _frag)) {
      return GL_FALSE;
    }
    glAttachShader(prog, frag);
  }
  glLinkProgram(prog);
  glGetProgramiv(prog, GL_LINK_STATUS, &status);
  glGetProgramInfoLog(prog, 8192, &len, info);
  if (len > 0) {
    printf("%s", info);
  }
  if (status != GL_TRUE) {
    printf("Failed to link program.\n");
    return GL_FALSE;
  }
  glUseProgram(prog);
  *_prog = prog;
  return GL_TRUE;
}

static GLint bind_texture(GLuint prog,const char *name, int tex) {
  GLint loc;
  loc = glGetUniformLocation(prog, name);
  if (loc < 0) {
    printf("Error finding texture '%s' in program %i\n", name, prog);
    return GL_FALSE;
  }
  glUniform1i(loc, tex);
  return GL_TRUE;
}

static const char *OPTSTRING = "h";

static const struct option OPTIONS[] = {
  { "help", no_argument, NULL, 'h' },
  { "no-cpu", no_argument, NULL, 0 },
  { "no-gpu", no_argument, NULL, 0 },
  { NULL, 0, NULL, 0 }
};

static void usage() {
  fprintf(stderr,
   "Usage: %s [options] jpeg_file\n\n"
   "Options:\n\n"
   "  -h --help                      Display this help and exit.\n"
   "     --no-cpu                    Disable CPU decoding in main loop.\n"
   "     --no-gpu                    Disable GPU decoding in main loop.\n"
   " %s accepts only 8-bit non-hierarchical JPEG files.\n\n", NAME, NAME);
}

int main(int argc, char *argv[]) {
  unsigned char *jpeg_buf;
  int jpeg_sz;
  image img;
  int no_cpu;
  int no_gpu;
  no_cpu = 0;
  no_gpu = 0;
  {
    int c;
    int loi;
    while ((c = getopt_long(argc, argv, OPTSTRING, OPTIONS, &loi)) != EOF) {
      switch (c) {
        case 0 : {
          if (strcmp(OPTIONS[loi].name, "no-cpu") == 0) {
            no_cpu = 1;
          }
          else if (strcmp(OPTIONS[loi].name, "no-gpu") == 0) {
            no_gpu = 1;
          }
          break;
        }
        case 'h' :
        default : {
          usage();
          return EXIT_FAILURE;
        }
      }
    }
    /*Assume anything following the options is a file name.*/
    jpeg_buf = NULL;
    jpeg_sz = 0;
    for (; optind < argc; optind++) {
      FILE *fp;
      int size;
      fp = fopen(argv[optind], "rb");
      if (fp == NULL) {
        fprintf(stderr, "Error, could not open jpeg file %s\n", argv[optind]);
        return EXIT_FAILURE;
      }
      fseek(fp, 0, SEEK_END);
      jpeg_sz = ftell(fp);
      free(jpeg_buf);
      jpeg_buf = malloc(jpeg_sz);
      if (jpeg_buf == NULL) {
        fprintf(stderr, "Error, could not allocate %i bytes\n", jpeg_sz);
        return EXIT_FAILURE;
      }
      fseek(fp, 0, SEEK_SET);
      size = fread(jpeg_buf, 1, jpeg_sz, fp);
      if (size != jpeg_sz) {
        fprintf(stderr, "Error reading jpeg file, got %i of %i bytes\n", size,
         jpeg_sz);
        return EXIT_FAILURE;
      }
      fclose(fp);
    }
  }
  if (jpeg_sz == 0) {
    usage();
    return EXIT_FAILURE;
  }

  /* Decompress the image header and allocate texture memory.
     We will have libjpeg directly decode the YUV image data into these
      buffers so they can be uploaded to the GPU. */
  {
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    int hmax, vmax;
    int i;

    cinfo.err=jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);

    jpeg_mem_src(&cinfo, jpeg_buf, jpeg_sz);
    if (jpeg_read_header(&cinfo, TRUE) == JPEG_HEADER_OK) {
      printf("read headers!\n");
    }

    img.width = cinfo.image_width;
    img.height = cinfo.image_height;
    img.nplanes = cinfo.num_components;
    printf("width = %i, height = %i\n", img.width, img.height);

    if (cinfo.num_components != NPLANES_MAX) {
      fprintf(stderr, "Unsupported number of components %i\n",
       cinfo.num_components);
    }

    hmax = 0;
    vmax = 0;
    for (i = 0; i < img.nplanes; i++) {
      jpeg_component_info *comp;
      comp = &cinfo.comp_info[i];
      if (comp->h_samp_factor > hmax) {
        hmax = comp->h_samp_factor;
      }
      if (comp->v_samp_factor > vmax) {
        vmax = comp->v_samp_factor;
      }
    }

    for (i = 0; i < cinfo.num_components; i++) {
      jpeg_component_info *comp;
      image_plane *plane;
      comp = &cinfo.comp_info[i];
      plane = &img.plane[i];
      plane->width = comp->width_in_blocks << 3;
      plane->height = comp->height_in_blocks << 3;
      plane->xstride = 1;
      plane->ystride = plane->xstride*plane->width;
      plane->xdec = OD_ILOG(hmax) - OD_ILOG(comp->h_samp_factor);
      plane->ydec = OD_ILOG(vmax) - OD_ILOG(comp->v_samp_factor);
      plane->data = malloc(plane->ystride*plane->height);
      printf("Plane %i: width = %4i, height = %4i, xdec = %i, ydec = %i\n",
       i, plane->width, plane->height, plane->xdec, plane->ydec);
    }

    jpeg_destroy_decompress(&cinfo);
  }

  /* Open a glfw context and run the entire libjpeg decoder inside the
      main loop.
     We decode only as far as the 8-bit YUV values and then upload these as
      textures to the GPU for the color conversion step.
     This should only upload half as much data as an RGB texture for 4:2:0
      images. */
  {
    GLFWwindow *window;
    GLuint tex[NPLANES_MAX];
    GLuint prog;
    GLuint vbo;
    GLuint vao;
    double last;
    int frames;
    int i;
    int first;
    int pixels;

    glfwSetErrorCallback(error_callback);
    if (!glfwInit()) {
      return EXIT_FAILURE;
    }

    glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    window = glfwCreateWindow(img.width, img.height, NAME, NULL, NULL);
    if (!window) {
      glfwTerminate();
      return EXIT_FAILURE;
    }

    glfwMakeContextCurrent(window);
    glfwSetKeyCallback(window, key_callback);
    glfwSwapInterval(0);

    printf("  OpenGL: %s\n",glGetString(GL_VERSION));
    printf("    GLSL: %s\n",glGetString(GL_SHADING_LANGUAGE_VERSION));
    printf("Renderer: %s\n",glGetString(GL_RENDERER));

    glViewport(0, 0, img.width, img.height);

    glGenTextures(img.nplanes, tex);
    for (i = 0; i < img.nplanes; i++) {
      image_plane *plane;
      plane = &img.plane[i];
      printf("Texture %i: %i\n", i, tex[i]);
      glActiveTexture(GL_TEXTURE0 + i);
      glBindTexture(GL_TEXTURE_2D, tex[i]);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, plane->width, plane->height, 0,
       GL_RED_INTEGER, GL_UNSIGNED_BYTE, plane->data);
    }

    switch (img.nplanes) {
      case 1 : {
        /* TODO handle grey scale jpegs */
        break;
      }
      case 3 : {
        if (!setup_shader(&prog, YUV_VERT, YUV_FRAG)) {
          return EXIT_FAILURE;
        }
        if (!bind_texture(prog, "y_tex", 0)) {
          return EXIT_FAILURE;
        }
        if (!bind_texture(prog, "u_tex", 1)) {
          return EXIT_FAILURE;
        }
        if (!bind_texture(prog, "v_tex", 2)) {
          return EXIT_FAILURE;
        }
        break;
      }
    }

    /* Create the vertex buffer object */
    {
      GLint in_pos;
      GLint in_tex;
      vertex v[4];

      in_pos = glGetAttribLocation(prog, "in_pos");
      printf("in_pos %i\n", in_pos);

      in_tex = glGetAttribLocation(prog, "in_tex");
      printf("in_tex %i\n", in_tex);

      /* Set the vertex world positions */
      v[0].x =  1.0; v[0].y =  1.0; v[1].z = 0.0;
      v[1].x =  1.0; v[1].y = -1.0; v[2].z = 0.0;
      v[2].x = -1.0; v[2].y =  1.0; v[0].z = 0.0;
      v[3].x = -1.0; v[3].y = -1.0; v[3].z = 0.0;

      /* Set the vertex texture coordinates */
      v[0].s = img.width; v[0].t = 0;
      v[1].s = img.width; v[1].t = img.height;
      v[2].s = 0;         v[2].t = 0;
      v[3].s = 0;         v[3].t = img.height;

      glGenVertexArrays(1, &vao);
      glBindVertexArray(vao);

      glGenBuffers(1, &vbo);
      glBindBuffer(GL_ARRAY_BUFFER, vbo);

      glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);

      glVertexAttribPointer(in_pos, 3, GL_FLOAT, GL_FALSE, sizeof(vertex),
       (void *)0);

      glVertexAttribIPointer(in_tex, 2, GL_INT, sizeof(vertex), (void *)12);

      glEnableVertexAttribArray(in_pos);
      glEnableVertexAttribArray(in_tex);
    }

    glBindFragDataLocation(prog, 0, "color");
    glUseProgram(prog);

    first = 0;
    last = glfwGetTime();
    frames = 0;
    pixels = 0;
    for (i = 0; i < img.nplanes; i++) {
      image_plane *plane;
      plane = &img.plane[i];
      pixels += (plane->width >> plane->xdec)*(plane->height >> plane->ydec);
    }
    while (!glfwWindowShouldClose(window)) {
      int i;
      double time;

      if (!no_cpu) {
        struct jpeg_decompress_struct cinfo;
        struct jpeg_error_mgr jerr;

        /* This code assumes 4:2:0 */
        JSAMPROW yrow_pointer[16];
        JSAMPROW cbrow_pointer[16];
        JSAMPROW crrow_pointer[16];
        JSAMPROW *plane_pointer[3];

        cinfo.err=jpeg_std_error(&jerr);
        jpeg_create_decompress(&cinfo);

        plane_pointer[0] = yrow_pointer;
        plane_pointer[1] = cbrow_pointer;
        plane_pointer[2] = crrow_pointer;

        jpeg_mem_src(&cinfo, jpeg_buf, jpeg_sz);
        jpeg_read_header(&cinfo, TRUE);

        cinfo.raw_data_out = TRUE;
        cinfo.do_fancy_upsampling = FALSE;
        cinfo.dct_method = JDCT_IFAST;

        jpeg_start_decompress(&cinfo);

        while (cinfo.output_scanline<cinfo.output_height) {
          int j;

          for (i = 0; i < img.nplanes; i++) {
            image_plane *plane;
            int y_off;
            plane = &img.plane[i];
            y_off = cinfo.output_scanline >> plane->ydec;
            for (j = 0; j < 16 >> plane->ydec; j++) {
              plane_pointer[i][j]=
               &plane->data[(y_off + j)*plane->width];
            }
          }

          jpeg_read_raw_data(&cinfo,plane_pointer, 16);
        }

        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
      }

      if (first) {
        for (i = 0; i < img.nplanes; i++) {
          image_plane *plane;
          int j, k;
          plane = &img.plane[i];
          printf("plane %i\n", i);
          for (k = 0; k < plane->height; k++) {
            for (j = 0; j < plane->width; j++) {
              printf("%i ", plane->data[k*plane->width + j]);
            }
            printf("\n");
          }
          printf("\n");
        }
        first = 0;
      }

      if (!no_gpu) {
        for (i = 0; i < img.nplanes; i++) {
          image_plane *plane;
          plane = &img.plane[i];
          glActiveTexture(GL_TEXTURE0+i);
          glBindTexture(GL_TEXTURE_2D, tex[i]);
          glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, plane->width, plane->height,
           0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, plane->data);
        }

        glClear(GL_COLOR_BUFFER_BIT);

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        glfwSwapBuffers(window);
      }

      frames++;
      time = glfwGetTime();
      if (time - last >= 1.0) {
        double avg;
        char title[255];
        avg = 1000*(time - last)/frames;
        if (no_gpu) {
          sprintf(title, "%s - %4i FPS (%0.3f ms)", NAME, frames, avg);
        }
        else {
          sprintf(title, "%s - %4i FPS (%0.3f ms) %0.3f MBps", NAME, frames,
           avg, frames*(pixels/1000000.0));
        }
        glfwSetWindowTitle(window, title);
        frames = 0;
        last = time;
      }

      glfwPollEvents();
    }
    glfwDestroyWindow(window);

    glDeleteTextures(img.nplanes, tex);
  }

  free(jpeg_buf);
  image_clear(&img);
  return EXIT_SUCCESS;
}
