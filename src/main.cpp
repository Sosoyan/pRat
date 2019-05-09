#include <mutex>
#include <thread>
#include <vector>

#include <SDL2/SDL.h>

#include "log.h"
#include "sphere.h"
#include "moving_sphere.h"
#include "hitable_list.h"
#include "float.h"
#include "camera.h"
#include "material.h"
#include "bvh.h"
#include "aarect.h"
#include "box.h"
#include "constant_medium.h"
#include "surface_texture.h"

using namespace std;

void worker(bool* kill, int tc, int id, int nx, int ny, int ns, vector<vec3>* sum_pixels, unsigned int* pixels, hitable* world, camera* cam);

vec3 color(const ray& r, hitable *world, int depth)
{
    hit_record rec;
    if (world->hit(r, 0.001, FLT_MAX, rec)) 
    {
        ray scattered;
        vec3 attenuation;
        vec3 emitted = rec.mat_ptr->emitted(rec.u, rec.v, rec.p);
        float pdf;
        vec3 albedo;

//        if (depth > 1) logRay(r, rec.t); // Plot Rays
        
        if (depth < 3 && rec.mat_ptr->scatter(r, rec, albedo, scattered, pdf))
        {
            vec3 on_light = vec3(213 + drand48() * (243-213), 554, 227 + drand48() * (332-227));
            vec3 to_light = on_light - rec.p;
            float distance_squared = to_light.squared_length();
            to_light.make_unit_vector();
            
            if (dot(to_light, rec.normal) < 0)
                return emitted;
            
            float light_area = (343-213) * (332-227);
            float light_cosine = fabs(to_light.y());
            
            if (light_cosine < 0.000001)
                return emitted;
            
            pdf = distance_squared / (light_cosine * light_area);
            scattered  = ray(rec.p, to_light, r.time());

            return emitted + albedo * rec.mat_ptr->scattering_pdf(r, rec, scattered) * color(scattered, world, depth+1) / pdf;
        }
        else
            return emitted;
    }
    else
        return vec3(0,0,0);
}


hitable *cornell_box()
{
    hitable **list = new hitable*[9];
    int i = 0;
    material *red = new lambertian( new constant_texture(vec3(0.65, 0.05, 0.05)) );
    material *white = new lambertian( new constant_texture(vec3(0.73, 0.73, 0.73)) );
    material *green = new lambertian( new constant_texture(vec3(0.12, 0.45, 0.15)) );
    material *light = new diffuse_light( new constant_texture(vec3(15, 15, 15)) );
    list[i++] = new flip_normals(new yz_rect(0, 555, 0, 555, 555, green));
    list[i++] = new yz_rect(0, 555, 0, 555, 0, red);
    list[i++] = new xz_rect(213, 343, 227, 332, 554, light);
    list[i++] = new flip_normals(new xz_rect(0, 555, 0, 555, 555, white));
    list[i++] = new xz_rect(0, 555, 0, 555, 0, white);
    list[i++] = new flip_normals(new xy_rect(0, 555, 0, 555, 555, white));
    list[i++] = new translate(new rotate_y(new box(vec3(0, 0, 0), vec3(165, 165, 165), white), -18), vec3(130,0,65));
    list[i++] = new translate(new rotate_y(new box(vec3(0, 0, 0), vec3(165, 330, 165), white),  15), vec3(265,0,295));
    list[i++] = new sphere(vec3(200, 250, 200), 50, white);
    return new hitable_list(list, i);
}

void show_window(int w, int h, unsigned int *pixels)
{
    // Initialize SDL.
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
        return ;
    
    SDL_Window* win = SDL_CreateWindow("pRat", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, w, h, SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Renderer* renderer = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    SDL_Texture* img = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
    
    int pitch = w * sizeof(unsigned int);
    SDL_Event event;
    while (true)
    {
        if (SDL_PollEvent(&event))
            if (SDL_QUIT == event.type)
                break;
        
        SDL_UpdateTexture(img, NULL, pixels, pitch);
        SDL_RenderCopy(renderer, img, NULL, NULL);
        SDL_RenderPresent(renderer);
        this_thread::sleep_for(chrono::milliseconds(20));
    }
    
    // free all resources
    SDL_DestroyTexture(img);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(win);
    SDL_Quit();
}

int main()
{
    // Cornell Box
    hitable* world = cornell_box();
    vec3 lookfrom(278, 278, -800);
    vec3 lookat(278,278,0);

    int nx = 512;
    int ny = 512;
    int ns = 100;
    
    float dist_to_focus = 10.0;
    float aperture = 0;
    float vfov = 40.0;
    
    unsigned int *pixels = new unsigned int[nx*ny];
    vector<vec3> sum_pixels;
    sum_pixels.resize(nx*ny);
    camera* cam = new camera(lookfrom, lookat, vec3(0,1,0), vfov, float(nx)/float(ny), aperture, dist_to_focus, 0.0, 1.0);
    
    // Spawning threads
    bool kill_thread = false;
    int threadCount = 1;
    threadCount = thread::hardware_concurrency(); // Enable Multithreading
    thread* threads = new thread[threadCount];
    for (int id=0; id<threadCount; id++)
        threads[id] = thread(worker, &kill_thread, threadCount, id, nx, ny, ns, &sum_pixels, pixels, world, cam);
    
    // Wait until the window is closed
    show_window(nx, ny, pixels);
    
    // Terminate threads
    kill_thread = true;
    for (int id=0; id<threadCount; id++)
        threads[id].join();
    
    return EXIT_SUCCESS;
}

void worker(bool* kill, int tc, int id, int nx, int ny, int ns, vector<vec3>* sum_pixels, unsigned int* pixels, hitable* world, camera* cam)
{
    int ny1 = ny / tc * (++id);
    int ny2 = ny1 - (ny / tc);

    for (int s=0; s < ns; s++)
    {
        for (int j = ny1-1; j >= ny2; j--)
        {
            for (int i=0; i < nx; i++)
            {
                if (*kill)
                    return;
                
                int idx = nx * (ny-j-1) + i;
                
                float u = float(i + drand48()) / float(nx);
                float v = float(j + drand48()) / float(ny);
                ray r = cam->get_ray(u, v);
                vec3 col = color(r, world, 0);
                sum_pixels->at(idx) += col;

                if (s > 0)
                    col = sum_pixels->at(idx) / (s+1);
            
                col = vec3(sqrt(col[0]), sqrt(col[1]), sqrt(col[2]));
                
                // Converting to integers
                int ir = int(255.99*col[0]);
                int ig = int(255.99*col[1]);
                int ib = int(255.99*col[2]);
                
                lock_guard<mutex> lock(mutex);
                pixels[idx] = (ir << 16) + (ig << 8) + ib;
            }
        }
    }
}


