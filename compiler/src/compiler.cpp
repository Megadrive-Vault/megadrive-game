#include "compiler.h"
#include "slz/slz.h"

#define FIX32_INT_BITS  22
#define FIX32_FRAC_BITS (32 - FIX32_INT_BITS)
#define FIX32(value)    ((int) ((value) * (1 << FIX32_FRAC_BITS)))

template<class T>
std::string t_to_string(T i)
{
    std::stringstream ss;
    std::string s;
    ss << i;
    s = ss.str();

    return s;
}

FILE* data_file_h;
FILE* data_file_c;

vector<string> frames;
vector<bool> null_frames;

void compile_bitmap(string root_path, string bitmap_path)
{
    string bitmap_name = bitmap_path.substr(0, bitmap_path.size()-4);
    printf("compiling %s...\n", bitmap_name.c_str());

    FILE* bitmap = fopen((root_path+bitmap_path).c_str(), "rb");
    
    // bitmap header

    char bitmap_header[2];
    fread(bitmap_header, 1, 2, bitmap);
    unsigned int bitmap_size;
    fread(&bitmap_size, 4, 1, bitmap);
    unsigned int reserved;
    fread(&reserved, 4, 1, bitmap);
    unsigned int bitmap_offset;
    fread(&bitmap_offset, 4, 1, bitmap);
    
    // DIB header

    unsigned int dib_size;
    fread(&dib_size, 4, 1, bitmap);
    unsigned int bitmap_width;
    fread(&bitmap_width, 4, 1, bitmap);
    unsigned int bitmap_height;
    fread(&bitmap_height, 4, 1, bitmap);
    unsigned int reserved2;
    fread(&reserved2, 2, 1, bitmap);
    unsigned short bitmap_bpp;
    fread(&bitmap_bpp, 2, 1, bitmap);
    fread(&reserved2, 4, 1, bitmap);
    fread(&reserved2, 4, 1, bitmap);
    fread(&reserved2, 4, 1, bitmap);
    fread(&reserved2, 4, 1, bitmap);
    unsigned int bitmap_depth;
    fread(&bitmap_depth, 4, 1, bitmap);
    
    if (bitmap_depth==0)
        bitmap_depth = 16;
    if (bitmap_depth<3 || bitmap_depth>16 || bitmap_bpp!=4)
    {
        printf("INCORRECT DEPTH\n");
        fclose(bitmap);
        return;
    }

    int packed_width = (bitmap_width*bitmap_bpp)/16;
    int row_size = bitmap_width*bitmap_bpp/8;
    if (bitmap_width!=(packed_width*16)/bitmap_bpp)
    {
        printf("INCORRECT SIZE: %d,%d (%d,%f)\n", bitmap_width, (packed_width*16)/bitmap_bpp, bitmap_bpp, (bitmap_width*bitmap_bpp)/16.0f);
        fclose(bitmap);
        return;
    }
    
    bitmap_width = packed_width;

    fprintf(data_file_h, "    extern smeBitmap %s;\n", bitmap_name.c_str());
    
    FILE* packed_input = fopen((root_path+"tmp.in").c_str(), "wb");
    {
        // palette

        fseek(bitmap, dib_size+14, SEEK_SET);
        
        for (unsigned int c=0 ; c<bitmap_depth ; ++c)
        {
            unsigned char b;
            fread(&b, 1, 1, bitmap);
            unsigned char g;
            fread(&g, 1, 1, bitmap);
            unsigned char r;
            fread(&r, 1, 1, bitmap);
            unsigned char a;
            fread(&a, 1, 1, bitmap);
        
            unsigned short v = ((r*15/255)<<8)+((g*15/255)<<12)+((b*15/255)<<0);
            fwrite(&v, 2, 1, packed_input);
        }

        // read image data

        unsigned short* image = (unsigned short*)malloc(bitmap_width*bitmap_height*2);

        for (unsigned int y=0 ; y<bitmap_height ; ++y)
        {
            fseek(bitmap, bitmap_offset+(bitmap_height-y-1)*row_size, SEEK_SET);
            for (unsigned int x=0 ; x<bitmap_width ; ++x)
            {
                fread(image+y*bitmap_width+x, 2, 1, bitmap);
                ((unsigned char*)(image+y*bitmap_width+x))[0] += 1+(1<<4);
                ((unsigned char*)(image+y*bitmap_width+x))[1] += 1+(1<<4);
            }
        }

        fwrite(image, 2, bitmap_width*bitmap_height, packed_input);
        free(image);
    }
    fclose(packed_input);
    
    // compress

    packed_input = fopen((root_path+"tmp.in").c_str(), "rb");
    FILE* packed_output = fopen((root_path+"tmp.out").c_str(), "wb");
    compress(packed_input, packed_output, FORMAT_SLZ16);
    fclose(packed_output);
    fclose(packed_input);
    
    packed_output = fopen((root_path+"tmp.out").c_str(), "rb");
    fseek(packed_output, 0, SEEK_END);
    long size = ftell(packed_output);
    unsigned char* packed = (unsigned char*)malloc(size);
    fseek(packed_output, 0, SEEK_SET);
    fread(packed, 1, size, packed_output);
    fclose(packed_output);
    
    // write
    fprintf(data_file_c, "u8 %s_packed[] = {", bitmap_name.c_str());
    
    for (int i=0 ; i<size ; ++i)
    {
        fprintf(data_file_c, "%u", packed[i]);
        if (i<size-1)
            fprintf(data_file_c, ",");
    }
    free(packed);

    fprintf(data_file_c, "};\n");

    fprintf(data_file_c, "smeBitmap %s = {\n", bitmap_name.c_str());
    fprintf(data_file_c, "    %i,\n", bitmap_width);
    fprintf(data_file_c, "    %i,\n", bitmap_height);
    fprintf(data_file_c, "    %i,\n", bitmap_depth);
    fprintf(data_file_c, "    NULL,\n");
    fprintf(data_file_c, "    NULL,\n");
    fprintf(data_file_c, "    NULL,\n");
    fprintf(data_file_c, "    %s_packed\n", bitmap_name.c_str());
    fprintf(data_file_c, "};\n");
    fclose(bitmap);
}

int TilesCount;

float compile_solid_normal(vector<unsigned char>& tile)
{
    float vx = 0.0f;
    float vy = 0.0f;
    for (int y=1 ; y<7 ; ++y)
    for (int x=1 ; x<7 ; ++x)
    {
        if (tile[y*8+x]!=smeMAP_SOLID_Block)
        {
            if (tile[y*8+x-1]==smeMAP_SOLID_Block) vx -= 1.0f;
            if (tile[y*8+x+1]==smeMAP_SOLID_Block) vx += 1.0f;
            if (tile[(y-1)*8+x]==smeMAP_SOLID_Block) vy -= 1.0f;
            if (tile[(y+1)*8+x]==smeMAP_SOLID_Block) vy += 1.0f;
        }
    }

    float norm = sqrtf(vx*vx+vy*vy);
    if (norm!=0.0f)
        return atan2f(vy/norm, vx/norm);
    return 0.0f;
}

void compile_tiles(map<int, int>& bindings, string root_path, string tiles_path, bool solid, int offset)
{
    string tiles_name = tiles_path.substr(0, tiles_path.size()-4);
    printf(".. %s\n", tiles_path.c_str());
    
    FILE* bitmap = fopen((root_path+tiles_path).c_str(), "rb");
    
    // bitmap header
    char bitmap_header[2];      fread(bitmap_header, 1, 2, bitmap);
    unsigned int bitmap_size;   fread(&bitmap_size, 4, 1, bitmap);
    unsigned int reserved;      fread(&reserved, 4, 1, bitmap);
    unsigned int bitmap_offset; fread(&bitmap_offset, 4, 1, bitmap);
    
    // DIB header
    unsigned int dib_size;      fread(&dib_size, 4, 1, bitmap);
    unsigned int bitmap_width;  fread(&bitmap_width, 4, 1, bitmap);
    unsigned int bitmap_height; fread(&bitmap_height, 4, 1, bitmap);
    unsigned int reserved2;     fread(&reserved2, 2, 1, bitmap);
    unsigned short bitmap_bpp;  fread(&bitmap_bpp, 2, 1, bitmap);
    fread(&reserved2, 4, 1, bitmap);    fread(&reserved2, 4, 1, bitmap);
    fread(&reserved2, 4, 1, bitmap);    fread(&reserved2, 4, 1, bitmap);
    unsigned int bitmap_depth;  fread(&bitmap_depth, 4, 1, bitmap);
    if (bitmap_depth==0) bitmap_depth = 16;

    if (bitmap_depth<3 || bitmap_depth>16 || bitmap_bpp!=4)
    {
        printf("!!! INCORRECT DEPTH !!!\n");
    }
    else if ((bitmap_width/8)*8!=bitmap_width || (bitmap_height/8)*8!=bitmap_height)
    {
        printf("!!! INCORRECT SIZE !!!\n");
    }
    else
    {
        // palette
        if (!solid)
        {
            fseek(bitmap, dib_size+14, SEEK_SET);        
            fprintf(data_file_c, "u16 %s_palette[] = {", tiles_name.c_str());    
            for (unsigned int c=0 ; c<bitmap_depth ; ++c)
            {
                unsigned char b;
                fread(&b, 1, 1, bitmap);
                unsigned char g;
                fread(&g, 1, 1, bitmap);
                unsigned char r;
                fread(&r, 1, 1, bitmap);
                unsigned char a;
                fread(&a, 1, 1, bitmap);
            
                unsigned short v = ((r*15/255)<<0)+((g*15/255)<<4)+((b*15/255)<<8);
                fprintf(data_file_c, "%d, ", v);    
            }
            fprintf(data_file_c, "0};\n");    
        }

        // tiles data
        int row_size = bitmap_width*bitmap_bpp/8;
        unsigned char* data = (unsigned char*)malloc(bitmap_height*row_size);
        for (unsigned int y=0 ; y<bitmap_height ; ++y)
        {
            fseek(bitmap, bitmap_offset+(bitmap_height-y-1)*row_size, SEEK_SET);
            fread(data+y*row_size, 1, row_size, bitmap);
        }

        int w = bitmap_width/8;
        int h = bitmap_height/8;
        fprintf(data_file_c, "const u8 %s_tiles_data[] = {\n", tiles_name.c_str());    
        
        bindings[-1] = 0;
        int tiles_count = 0;
        int tiles_address = offset;
        vector<string> tiles;
        vector<vector<unsigned char>> solid_tiles_data;

        for (int y=0 ; y<h ; ++y)
        for (int x=0 ; x<w ; ++x)
        {
            string tile_str = "    ";
            vector<unsigned char> solid_tile;

            // Store string values and check if its empty
            int empty = 1;
            for (int j=0 ; j<8 ; ++j)
            for (int i=0 ; i<4 ; ++i)
            {
                unsigned char v = *(data+(y*8+j)*row_size+x*4+i);
                if (v!=0)
                    empty = 0;
                
                if (solid)
                {
                    int a = v&15;
                    if (a>=smeMAP_SOLID_Count) a = 0;
                    solid_tile.push_back(a);
                    int b = v>>4;
                    if (b>=smeMAP_SOLID_Count) b = 0;
                    solid_tile.push_back(b);                    
                }
            
                tile_str += t_to_string((int)v)+",";
            }

            if (empty==1 && (!solid || tiles_count>0))
            {
                // Empty, bind to null tile
                bindings[y*w+x] = 0;
            }
            else
            {
                // Check if tile already exist
                int id = -1;
                for (int i=0 ; i<tiles.size() ; ++i)
                {
                    if (tiles[i]==tile_str)
                    {
                        id = i;
                        break;
                    }
                }
                if (id==-1)
                {
                    // Write it!
                    bindings[y*w+x] = tiles_count+tiles_address;
                    fprintf(data_file_c, "%s\n", tile_str.c_str());
                    ++tiles_count;
                    tiles.push_back(tile_str);
                    
                    if (solid)
                        solid_tiles_data.push_back(solid_tile);
                }
                else
                {
                    bindings[y*w+x] = id+tiles_address;
                }
            }            
        }
        fprintf(data_file_c, "0};\n");

        TilesCount = tiles_count;

        if (solid)
        {
            fprintf(data_file_c, "const fix32 %s_tiles_normal[] = {\n", tiles_name.c_str());    
            for (int i=0 ; i<solid_tiles_data.size() ; ++i)
                fprintf(data_file_c, "%d,", FIX32(compile_solid_normal(solid_tiles_data[i])));
            fprintf(data_file_c, "0};\n");
        }
        else
        {
            fprintf(data_file_c, "const TileSet %s_tiles = {0, %d, %s_tiles_data};\n", tiles_name.c_str(), tiles_count, tiles_name.c_str());    
        }
        free(data);
    }
    fclose(bitmap);
}

void compile_data(string root_path, string map_path, map<int, int>* solid_bindings, int offset)
{
    printf(".. %s\n", map_path.c_str());
    string map_name = map_path.substr(0, map_path.size()-4);
    
    map<int, int> graphics_bindings;
    map<int, int>& bindings = graphics_bindings;
    if (solid_bindings==NULL) compile_tiles(bindings, root_path, map_name+".bmp", false, offset);
    else bindings = *solid_bindings;

    fprintf(data_file_c, "const u16 %s[] = {", map_name.c_str());
    
    FILE* map = fopen((root_path+map_path).c_str(), "rt");
    int tile_index;
    unsigned int id = 0; 
    while (feof(map)==0 && fscanf(map, "%d", &tile_index))
    {
        fprintf(data_file_c, "%d,", bindings[tile_index]);
        fgetc(map);
        ++id;
        if (id>128)
        {
            fprintf(data_file_c, "\n");
            id = 0;
        }
    }
    fclose(map);
    fprintf(data_file_c, "0};\n\n");
}

void compile_plane(string root_path, string map_name, map<int, int>& solid_bindings, int offset)
{
    compile_data(root_path, map_name+"_solid.csv", &solid_bindings, 0);
    compile_data(root_path, map_name+"_graphics.csv", NULL, offset);

    fprintf(data_file_c, "smePlane %s = {\n", map_name.c_str());
    fprintf(data_file_c, "    %s_solid,\n", map_name.c_str());
    fprintf(data_file_c, "    %s_graphics,\n", map_name.c_str());
    fprintf(data_file_c, "    %s_graphics_palette,\n", map_name.c_str());
    fprintf(data_file_c, "    &%s_graphics_tiles,\n", map_name.c_str());
    fprintf(data_file_c, "    NULL\n");
    fprintf(data_file_c, "    };\n");    
}

void compile_map(string root_path, string map_path)
{
    string map_name = map_path.substr(0, map_path.size()-4);
    printf("compiling map %s...\n", map_name.c_str());

    FILE* file = fopen((root_path+map_path).c_str(), "rt");
    char header[2048];
    fread(header, 1, 2048, file);
    fclose(file);

    fprintf(data_file_h, "    extern smeMap %s;\n", map_name.c_str());
    
    char width[12];
    int i=0; while (strncmp(header+i, "width", 5)!=0) ++i;
    int j=7; while (header[i+j]!='\"') { width[j-7] = header[i+j]; ++j; } width[j-7] = '\0';
    char height[12];
    i=0; while (strncmp(header+i, "height", 6)!=0) ++i;
    j=8; while (header[i+j]!='\"') { height[j-8] = header[i+j]; ++j; } height[j-8] = '\0';

    // Physics

    map<int, int> solid_bindings;
    compile_tiles(solid_bindings, root_path, map_name+"_solid.bmp", true, 0);

    // Planes
    compile_plane(root_path, map_name+"_plan_a", solid_bindings, 0x10);
    compile_plane(root_path, map_name+"_plan_b", solid_bindings, 0x10+TilesCount);
    
    // Write it
    fprintf(data_file_c, "smeMap %s = {\n", map_name.c_str());
    fprintf(data_file_c, "    %s,\n", width);
    fprintf(data_file_c, "    %s,\n", height);
    fprintf(data_file_c, "    %s_solid_tiles_data,\n", map_name.c_str());
    fprintf(data_file_c, "    %s_solid_tiles_normal,\n", map_name.c_str());
    fprintf(data_file_c, "    &%s_plan_a,\n", map_name.c_str());
    fprintf(data_file_c, "    &%s_plan_b\n", map_name.c_str());
    fprintf(data_file_c, "    };\n");    
}

int main(int argc, char* argv[])
{
    string root_path = argv[1];
    printf("compiling data...\n");

    data_file_h = fopen((root_path+"data.h").c_str(), "wt");
    fprintf(data_file_h, "#ifndef __DATA_H__\n");
    fprintf(data_file_h, "#define __DATA_H__\n\n");
    fprintf(data_file_h, "    #include \"sme.h\"\n\n");

    data_file_c = fopen((root_path+"data.c").c_str(), "wt");
    fprintf(data_file_c, "#include \"data.h\"\n\n");

    /*
    // Bitmaps
    {
        WIN32_FIND_DATA find_data;
        HANDLE handle = FindFirstFile((root_path+"*.bmp").c_str(), &find_data);
        if (handle!=INVALID_HANDLE_VALUE)
        {
            do
            {
                if (!(find_data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))
                    compile_bitmap(root_path, find_data.cFileName);
            }
            while (FindNextFile(handle, &find_data)!=0);
        }
        FindClose(handle);
    }
*/

    // Maps
    {
        WIN32_FIND_DATA find_data;
        HANDLE handle = FindFirstFile((root_path+"*.tmx").c_str(), &find_data);
        if (handle!=INVALID_HANDLE_VALUE)
        {
            do
            {
                if (!(find_data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))
                    compile_map(root_path, find_data.cFileName);
            }
            while (FindNextFile(handle, &find_data)!=0);
        }
        FindClose(handle);
    }

    fprintf(data_file_h, "\n#endif\n");
    fclose(data_file_h);
    return 0;
}

