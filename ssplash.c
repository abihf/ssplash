#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <termios.h>

#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <linux/fb.h>
#include <linux/vt.h>
#include <linux/kd.h>

#include "lodepng.h"

#define FADE_IN_STEPS 30
#define LOADER_Y_OFFSET 710

typedef struct
{
  unsigned char r;
  unsigned char g;
  unsigned char b;
} Pixel;

int fbfd = 0;
long int screensize = 0;
unsigned char *fbp = 0;

Pixel *splash_img;
unsigned splash_width;
unsigned splash_height;

struct fb_var_screeninfo vinfo;
struct fb_fix_screeninfo finfo;

int console_fd = -1;
int vt_visible = 1;
int vtnum = 7;
int running = 1;

static inline Pixel *get_pixel(int x, int y)
{
  return &splash_img[y * splash_width + x];
}

static void ssplash_fb_set_pixel(char *fbp, int x, int y, unsigned char r, unsigned char g, unsigned char b)
{
  int offset = 0;

  while (!vt_visible && running)
  {
    printf("waiting semaphore\n");
    usleep(1000);
  }
  if (!running)
    return;

  offset = (x + vinfo.xoffset) * (vinfo.bits_per_pixel / 8) +
           (y + vinfo.yoffset) * finfo.line_length;

  if (vinfo.bits_per_pixel == 32)
  {
    *(fbp + offset) = b;
    *(fbp + offset + 1) = g;
    *(fbp + offset + 2) = r;
  }
  else if (vinfo.bits_per_pixel == 16)
  {
    *(volatile uint16_t *)(fbp + offset) =
        ((r >> (8 - vinfo.red.length)) << vinfo.red.offset) | ((g >> (8 - vinfo.green.length)) << vinfo.green.offset) | ((b >> (8 - vinfo.blue.length)) << vinfo.blue.offset);
  }
}

static void ssplash_animate_fade(int fade_out)
{
  unsigned char *screenbuffer = 0, *bgr = 0;
  unsigned char dark;
  int i, x, y;

  screenbuffer = (unsigned char *)malloc(screensize);
  if (screenbuffer == (void *)-1)
  {
    perror("Error: failed allocate buffer");
    exit(4);
  }

  bgr = (unsigned char *)malloc(screensize);
  if (screenbuffer == (void *)-1)
  {
    perror("Error: failed allocate buffer");
    exit(4);
  }
  memset(bgr, 0, screensize);

  Pixel *pix;
  for (y = 0; y < vinfo.yres; y++)
  {
    for (x = 0; x < vinfo.xres; x++)
    {
      pix = get_pixel(x, y);

      ssplash_fb_set_pixel(bgr, x, y,
                           pix->r,
                           pix->g,
                           pix->b);
    }
  }

  struct timeval currentTime;

  for (i = 0; i <= FADE_IN_STEPS && running; i++)
  {
    gettimeofday(&currentTime, NULL);
    double factor = (double)i / (double)FADE_IN_STEPS;
    if (fade_out)
    {
      factor = 1 - factor;
    }
    for (size_t p = 0; p < screensize; p++)
    {
      screenbuffer[p] = bgr[p] * factor;
    }

    static unsigned int arg = 0;
    ioctl(fbfd, FBIO_WAITFORVSYNC, &arg);
    memcpy(fbp, screenbuffer, screensize);
    // printf("Drawing %d\n", currentTime.tv_usec);
  }
  free(screenbuffer);
  free(bgr);
}

static void
ssplash_vt_request(int sig)
{
  if (vt_visible)
  {
    /* Allow Switch Away */
    if (ioctl(console_fd, VT_RELDISP, 1) < 0)
      perror("Error cannot switch away from console");
    vt_visible = 0;
  }
  else
  {
    if (ioctl(console_fd, VT_RELDISP, VT_ACKACQ))
      perror("Error can't acknowledge VT switch");
    vt_visible = 1;
    /* FIXME: need to schedule repaint some how ? */
  }
}

static void
ssplash_console_handle_switches(void)
{
  struct sigaction act;
  struct vt_mode vt_mode;

  if (ioctl(console_fd, VT_GETMODE, &vt_mode) < 0)
  {
    perror("Error VT_SETMODE failed");
    return;
  }

  act.sa_handler = ssplash_vt_request;
  sigemptyset(&act.sa_mask);
  act.sa_flags = 0;
  sigaction(SIGUSR1, &act, 0);

  vt_mode.mode = VT_PROCESS;
  vt_mode.relsig = SIGUSR1;
  vt_mode.acqsig = SIGUSR1;

  if (ioctl(console_fd, VT_SETMODE, &vt_mode) < 0)
    perror("Error VT_SETMODE failed");
}

static void
ssplash_console_ignore_switches(void)
{
  struct sigaction act;
  struct vt_mode vt_mode;

  if (ioctl(console_fd, VT_GETMODE, &vt_mode) < 0)
  {
    perror("Error VT_SETMODE failed");
    return;
  }

  act.sa_handler = SIG_IGN;
  sigemptyset(&act.sa_mask);
  act.sa_flags = 0;
  sigaction(SIGUSR1, &act, 0);

  vt_mode.mode = VT_AUTO;
  vt_mode.relsig = 0;
  vt_mode.acqsig = 0;

  if (ioctl(console_fd, VT_SETMODE, &vt_mode) < 0)
    perror("Error VT_SETMODE failed");
}

static void ssplash_console_switch()
{
  char vtname[10];
  struct vt_stat vt_state;

  sprintf(vtname, "/dev/tty%d", vtnum);
  if ((console_fd = open(vtname, O_RDWR)) < 0)
  {
    perror("Unable to open vt");
  }

  // ssplash_console_ignore_switches();
  if (ioctl(console_fd, KDSETMODE, KD_GRAPHICS) < 0)
    perror("Error KDSETMODE KD_GRAPHICS failed\n");

  if (ioctl(console_fd, VT_ACTIVATE, vtnum) != 0)
    perror("Error VT_ACTIVATE failed");

  if (ioctl(console_fd, VT_WAITACTIVE, vtnum) != 0)
    perror("Error VT_WAITACTIVE failed\n");

  // ssplash_console_handle_switches();
}

static void ssplash_console_clean()
{
  int fd;

  ioctl(console_fd, KDSETMODE, KD_TEXT);
  ssplash_console_ignore_switches();
  close(console_fd);

  if ((fd = open("/dev/tty0", O_RDWR | O_NDELAY, 0)) >= 0)
  {
    ioctl(fd, VT_DISALLOCATE, vtnum);
    close(fd);
  }
}

static void ssplash_exit(int signum)
{
  running = 0;

  ssplash_console_clean();
  if (fbp)
    munmap(fbp, screensize);

  if (fbfd)
    close(fbfd);
}

int main(int argc, char **argv)
{
  char *filename;
  int show_fade = 0;
  unsigned error;

  int is_shutdown = argc > 1 && strcmp(argv[1], "shutdown") == 0;

  // if (!is_shutdown)
  // {
  //   if (fork() != 0)
  //   {
  //     argv[0] = "systemd";
  //     execv("/usr/lib/systemd/systemd", argv);
  //   }
  // }

  show_fade = 1;
  filename = "/etc/splash.png";

  unsigned char *buf;
  error = lodepng_decode24_file(&buf, &splash_width, &splash_height, filename);
  if (error)
  {
    perror("error (%d) cannot load png file\n");
    return error;
  }
  splash_img = (Pixel *)buf;

  //sem_init(&sem, 0, 0);

  signal(SIGHUP, ssplash_exit);
  signal(SIGINT, ssplash_exit);
  signal(SIGQUIT, ssplash_exit);

  ssplash_console_switch();

  // Open the file for reading and writing
  fbfd = open("/dev/fb0", O_RDWR);
  if (fbfd == -1)
  {
    perror("Error: cannot open framebuffer device");
    exit(1);
  }

  // Get variable screen information
  if (ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo) == -1)
  {
    perror("Error reading variable information");
    exit(3);
  }

  // Get fixed screen information
  if (ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo) == -1)
  {
    perror("Error reading fixed information");
    exit(2);
  }

  // Figure out the size of the screen in bytes
  screensize = finfo.line_length * vinfo.yres;
  fbp = (unsigned char *)mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
  if (fbp == (void *)-1)
  {
    perror("Error: failed to map framebuffer device to memory");
    exit(4);
  }

  memset(fbp, 0, screensize);
  ssplash_animate_fade(is_shutdown);

  free(buf);
  exit(0);
}
