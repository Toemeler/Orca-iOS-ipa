// Minimal STL reader (binary + ASCII) for the iPad viewer app.
// Produces flat vertex/normal arrays ready for glVertexPointer/glNormalPointer.
// License: AGPL-3.0.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

struct Mesh
{
    std::vector<float> verts;    // 3 floats per vertex, 3 vertices per triangle
    std::vector<float> normals;  // parallel to verts
    size_t      triangles = 0;
    float       lo[3] = {0, 0, 0};
    float       hi[3] = {0, 0, 0};
    bool        ok = false;
    std::string error;

    float size(int axis) const { return hi[axis] - lo[axis]; }
    float centre(int axis) const { return (hi[axis] + lo[axis]) * 0.5f; }
};

namespace stl_detail {

inline void addTriangle(Mesh& m, const float v[9], const float n[3])
{
    // STL facet normals are frequently zero or garbage; recompute from the
    // winding when the stored one is not usable, otherwise lighting goes black.
    float nx = n[0], ny = n[1], nz = n[2];
    if ( !(std::fabs(nx) + std::fabs(ny) + std::fabs(nz) > 1e-12f) )
    {
        const float ax = v[3] - v[0], ay = v[4] - v[1], az = v[5] - v[2];
        const float bx = v[6] - v[0], by = v[7] - v[1], bz = v[8] - v[2];
        nx = ay * bz - az * by;
        ny = az * bx - ax * bz;
        nz = ax * by - ay * bx;
    }
    const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if ( len > 1e-20f ) { nx /= len; ny /= len; nz /= len; }

    for ( int i = 0; i < 3; ++i )
    {
        const float* p = v + i * 3;
        m.verts.insert(m.verts.end(), p, p + 3);
        m.normals.push_back(nx);
        m.normals.push_back(ny);
        m.normals.push_back(nz);

        for ( int a = 0; a < 3; ++a )
        {
            if ( m.triangles == 0 && i == 0 ) { m.lo[a] = m.hi[a] = p[a]; }
            else
            {
                if ( p[a] < m.lo[a] ) m.lo[a] = p[a];
                if ( p[a] > m.hi[a] ) m.hi[a] = p[a];
            }
        }
    }
    ++m.triangles;
}

inline bool readBinary(FILE* f, long fileSize, Mesh& m)
{
    if ( fileSize < 84 ) return false;
    std::fseek(f, 80, SEEK_SET);
    uint32_t count = 0;
    if ( std::fread(&count, 4, 1, f) != 1 ) return false;

    // The count must account for the file exactly, otherwise this is ASCII.
    if ( static_cast<long>(84 + 50ull * count) != fileSize ) return false;

    m.verts.reserve(count * 9);
    m.normals.reserve(count * 9);
    for ( uint32_t i = 0; i < count; ++i )
    {
        float buf[12];
        uint16_t attr;
        if ( std::fread(buf, 4, 12, f) != 12 ) return false;
        if ( std::fread(&attr, 2, 1, f) != 1 ) return false;
        addTriangle(m, buf + 3, buf);
    }
    return true;
}

inline bool readAscii(FILE* f, Mesh& m)
{
    std::fseek(f, 0, SEEK_SET);
    char line[512];
    float n[3] = {0, 0, 0};
    float v[9];
    int   vi = 0;

    while ( std::fgets(line, sizeof(line), f) )
    {
        const char* p = line;
        while ( *p == ' ' || *p == '\t' ) ++p;

        if ( std::strncmp(p, "facet normal", 12) == 0 )
        {
            if ( std::sscanf(p + 12, "%f %f %f", &n[0], &n[1], &n[2]) != 3 )
                n[0] = n[1] = n[2] = 0.0f;
            vi = 0;
        }
        else if ( std::strncmp(p, "vertex", 6) == 0 && vi < 3 )
        {
            if ( std::sscanf(p + 6, "%f %f %f",
                             &v[vi * 3], &v[vi * 3 + 1], &v[vi * 3 + 2]) == 3 )
            {
                if ( ++vi == 3 ) { addTriangle(m, v, n); vi = 0; }
            }
        }
    }
    return m.triangles > 0;
}

} // namespace stl_detail

inline Mesh loadSTL(const std::string& path)
{
    Mesh m;
    FILE* f = std::fopen(path.c_str(), "rb");
    if ( !f ) { m.error = "cannot open file"; return m; }

    std::fseek(f, 0, SEEK_END);
    const long sz = std::ftell(f);

    if ( stl_detail::readBinary(f, sz, m) )
        m.ok = m.triangles > 0;
    else
    {
        m = Mesh();
        m.ok = stl_detail::readAscii(f, m);
    }

    std::fclose(f);
    if ( !m.ok && m.error.empty() ) m.error = "not a readable STL";
    return m;
}

// A unit cube, so the app shows something useful before any file is added.
inline Mesh makeCube(float mm = 20.0f)
{
    const float h = mm * 0.5f;
    const float c[8][3] = {
        {-h,-h,-h}, { h,-h,-h}, { h, h,-h}, {-h, h,-h},
        {-h,-h, h}, { h,-h, h}, { h, h, h}, {-h, h, h}};
    const int faces[12][3] = {
        {0,2,1},{0,3,2}, {4,5,6},{4,6,7}, {0,1,5},{0,5,4},
        {1,2,6},{1,6,5}, {2,3,7},{2,7,6}, {3,0,4},{3,4,7}};

    Mesh m;
    const float zero[3] = {0, 0, 0};
    for ( const auto& t : faces )
    {
        float v[9];
        for ( int i = 0; i < 3; ++i )
            std::memcpy(v + i * 3, c[t[i]], sizeof(float) * 3);
        stl_detail::addTriangle(m, v, zero);
    }
    m.ok = true;
    return m;
}
