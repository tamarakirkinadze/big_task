#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "lodepng.h"

typedef struct {
    unsigned char r, g, b, a;
} Pixel;

typedef struct {
    int pixel_cnt;
    unsigned char avg_r, avg_g, avg_b;
} Component;

typedef struct {
    int x, y;
} Point;

typedef struct {
    Point* data;
    int front, rear, size;
} Queue;

void find_components(Pixel* image, int* pixel_comp, Component* components, 
                    int width, int height, int threshold);

Queue* createQueue(int size) {
    Queue* queue = (Queue*)malloc(sizeof(Queue));
    queue->size = size;
    queue->front = queue->rear = 0;
    queue->data = (Point*)malloc(size * sizeof(Point));
    return queue;
}

void freeQueue(Queue* queue) {
    free(queue->data);
    free(queue);
}

Pixel* load_image(const char* filename, unsigned* width, unsigned* height) {
    unsigned char* data = NULL;
    unsigned error = lodepng_decode32_file(&data, width, height, filename);
    
    if (error) {
        printf("Error %u: %s\n", error, lodepng_error_text(error));
        return NULL;
    }

    Pixel* image = (Pixel*)malloc(*width * *height * sizeof(Pixel));
    for (unsigned i = 0; i < *width * *height; i++) {
        image[i].r = data[4*i];
        image[i].g = data[4*i+1];
        image[i].b = data[4*i+2];
        image[i].a = data[4*i+3];
    }
    free(data);
    return image;
}

void save_image(const char* filename, Pixel* image, unsigned width, unsigned height) {
    unsigned char* data = (unsigned char*)malloc(width * height * 4);
    for (unsigned i = 0; i < width * height; i++) {
        data[4*i]   = image[i].r;
        data[4*i+1] = image[i].g;
        data[4*i+2] = image[i].b;
        data[4*i+3] = image[i].a;
    }

    unsigned error = lodepng_encode32_file(filename, data, width, height);
    if (error) printf("Error %u: %s\n", error, lodepng_error_text(error));
    free(data);
}

void sobel_filter(Pixel* image, Pixel* out_image, int width, int height) {
    int gx[3][3] = {{-1, 0, 1}, {-2, 0, 2}, {-1, 0, 1}};
    int gy[3][3] = {{1, 2, 1}, {0, 0, 0}, {-1, -2, -1}};

    for (int y = 1; y < height-1; y++) {
        for (int x = 1; x < width-1; x++) {
            int sx_r = 0, sx_g = 0, sx_b = 0;
            int sy_r = 0, sy_g = 0, sy_b = 0;

            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    Pixel p = image[(y+dy)*width + (x+dx)];
                    int kernel_x = gx[dy+1][dx+1];
                    int kernel_y = gy[dy+1][dx+1];
                    
                    sx_r += kernel_x * p.r;
                    sx_g += kernel_x * p.g;
                    sx_b += kernel_x * p.b;
                    
                    sy_r += kernel_y * p.r;
                    sy_g += kernel_y * p.g;
                    sy_b += kernel_y * p.b;
                }
            }

            int grad_r = (int)sqrt(sx_r*sx_r + sy_r*sy_r);
            int grad_g = (int)sqrt(sx_g*sx_g + sy_g*sy_g);
            int grad_b = (int)sqrt(sx_b*sx_b + sy_b*sy_b);
            
            int grad = (grad_r + grad_g + grad_b) / 3;
            grad = grad > 255 ? 255 : (grad < 0 ? 0 : grad);

            out_image[y*width + x] = (Pixel){
                .r = (unsigned char)grad,
                .g = (unsigned char)grad,
                .b = (unsigned char)grad,
                .a = 255
            };
        }
    }
}

void BFS(Pixel* image, int* pixel_comp, Component* components, 
         int width, int height, int x, int y, int component_id, int threshold) {
    int dx[] = {-1, 1, 0, 0};
    int dy[] = {0, 0, -1, 1};
    
    Queue* queue = createQueue(width * height);
    queue->data[queue->rear++] = (Point){x, y};
    pixel_comp[y*width + x] = component_id;
    
    Pixel seed = image[y*width + x];
    int total_r = seed.r, total_g = seed.g, total_b = seed.b;
    int count = 1;

    while (queue->front != queue->rear) {
        Point p = queue->data[queue->front++];
        
        for (int i = 0; i < 4; i++) {
            int nx = p.x + dx[i];
            int ny = p.y + dy[i];
            
            if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
                int idx = ny * width + nx;
                if (pixel_comp[idx] == 0) {
                    Pixel current = image[idx];
                    
                    int diff = abs(seed.r - current.r) + 
                                abs(seed.g - current.g) + 
                                abs(seed.b - current.b);
                    
                    if (diff < threshold) {
                        pixel_comp[idx] = component_id;
                        queue->data[queue->rear++] = (Point){nx, ny};
                        
                        total_r += current.r;
                        total_g += current.g;
                        total_b += current.b;
                        count++;
                    }
                }
            }
        }
    }
    
    components[component_id] = (Component){
        .pixel_cnt = count,
        .avg_r = (unsigned char)(total_r / count),
        .avg_g = (unsigned char)(total_g / count),
        .avg_b = (unsigned char)(total_b / count)
    };
    
    freeQueue(queue);
}

void find_components(Pixel* image, int* pixel_comp, Component* components, 
                     int width, int height, int threshold) {
    int component_id = 1;
    
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * width + x;
            if (pixel_comp[idx] == 0 && image[idx].a > 128) {
                BFS(image, pixel_comp, components, width, height, x, y, component_id, threshold);
                component_id++;
            }
        }
    }
}

void generate_component_colors(Pixel* output, int* pixel_comp, Component* components, 
                              int width, int height, int num_components, Pixel* original) {
    Pixel segment_colors[] = {
        {255, 0, 0, 255},
        {174, 0, 255, 255}, 
        {0, 255, 0, 255},
        {255, 0, 170, 255},
        {0, 0, 255, 255} 
    };
    int num_colors = sizeof(segment_colors) / sizeof(segment_colors[0]);

    Component background = {0};
    for (int i = 1; i < num_components; i++) {
        if (components[i].pixel_cnt > background.pixel_cnt) {
            background = components[i];
        }
    }

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * width + x;
            int comp_id = pixel_comp[idx];
            
            if (comp_id == 0 || 
                (abs(components[comp_id].avg_r - background.avg_r) < 30 &&
                 abs(components[comp_id].avg_g - background.avg_g) < 30 &&
                 abs(components[comp_id].avg_b - background.avg_b) < 30)) {
                output[idx] = original[idx];
            } else {
                int color_idx = (comp_id - 1) % num_colors;
                output[idx] = segment_colors[color_idx];
                
                float brightness = (original[idx].r * 0.299f + 
                                  original[idx].g * 0.587f + 
                                  original[idx].b * 0.114f) / 255.0f;
                
                output[idx].r = (unsigned char)(segment_colors[color_idx].r * brightness);
                output[idx].g = (unsigned char)(segment_colors[color_idx].g * brightness);
                output[idx].b = (unsigned char)(segment_colors[color_idx].b * brightness);
            }
        }
    }
}

int main() {
    const char* input_file = "skull.png";
    const char* output_files[] = {
        "11_edges.png",
        "22_components.png",
        "33_result.png"
    };
    
    unsigned width, height;
    Pixel* image = load_image(input_file, &width, &height);
    if (!image) return 1;
    
    int size = width * height;
    
    Pixel* edges = (Pixel*)malloc(size * sizeof(Pixel));
    sobel_filter(image, edges, width, height);
    save_image(output_files[0], edges, width, height);
    
    int* pixel_comp = (int*)calloc(size, sizeof(int));
    Component* components = (Component*)calloc(size, sizeof(Component));
    
    find_components(image, pixel_comp, components, width, height, 30);
    
    Pixel* comp_vis = (Pixel*)malloc(size * sizeof(Pixel));
    for (int i = 0; i < size; i++) {
        int comp_id = pixel_comp[i];
        if (comp_id == 0) {
            comp_vis[i] = (Pixel){0, 0, 0, 255};
        } else {
            comp_vis[i] = (Pixel){
                components[comp_id].avg_r,
                components[comp_id].avg_g,
                components[comp_id].avg_b,
                255
            };
        }
    }
    save_image(output_files[1], comp_vis, width, height);
    
    Pixel* result = (Pixel*)malloc(size * sizeof(Pixel));
    generate_component_colors(result, pixel_comp, components, width, height, size, image);
    save_image(output_files[2], result, width, height);
    
    free(image);
    free(edges);
    free(pixel_comp);
    free(components);
    free(comp_vis);
    free(result);
    return 0;
}