#ifndef __UTILS_H__
#define __UTILS_H__
typedef struct Config
{
    
    double map_width;
    double map_height;
    double drone_x;
    double drone_y;
}Config;
int load_config(const char *filename, struct Config *config);

#endif