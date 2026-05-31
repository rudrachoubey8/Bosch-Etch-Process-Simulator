#version 430
layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;

struct Chunk {
    int chunkX;
    int chunkY;
    int chunkZ;
    int dirty;
    uint vertexOffset;
    uint vertexCount;
};

layout(std430, binding = 6) buffer ChunkBuffer {
    Chunk chunks[];
};

uniform ivec3 chunkGridSize;

void main() {
    ivec3 id = ivec3(gl_GlobalInvocationID);

    if(id.x >= chunkGridSize.x ||
       id.y >= chunkGridSize.y ||
       id.z >= chunkGridSize.z)
        return;

    int index = id.x
              + id.y * chunkGridSize.x
              + id.z * chunkGridSize.x * chunkGridSize.y;
    
    if(chunks[index].dirty == 1){
        chunks[index].vertexCount = 0;
        }

    chunks[index].dirty = 0;
}