/* Synthetic native scene tests. Optional BANK PREPARED_DS MODEL DEPTH
 * arguments dump a private fixture result for the Python/original-byte
 * comparison. No game assets are required by the registered CTest. */
#include "scene_mesh.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static uint8_t bank[SCENE_SEGMENT], ds[SCENE_SEGMENT];
static scene_pose pose;
static scene_geometry geometry;
static scene_material emitted[32];
static unsigned nemitted;
static void put(uint8_t *p, unsigned at, uint32_t v, unsigned n)
{ for (unsigned k = 0; k < n; k++) p[(uint16_t)(at + k)] = (uint8_t)(v >> (8 * k)); }
static void w(unsigned *at, int v) { put(bank, *at, (uint16_t)v, 2); *at += 2; }
static void b(unsigned *at, unsigned v) { bank[(uint16_t)(*at)++] = (uint8_t)v; }
static void fixture(int shared, int ordered)
{
    memset(bank, 0, sizeof bank); memset(ds, 0, sizeof ds);
    put(bank, 0, 1, 2); put(bank, 2, 0x100, 2);
    unsigned at = 0x100; w(&at, 5); w(&at, 0x200); w(&at, 32769); w(&at, 0x300);
    at = 0x300; w(&at, 65535); w(&at, 0); w(&at, 0); w(&at, 0); w(&at, 0); b(&at, 255);
    at = 0x200; w(&at, 65535);
    if (shared) {
        w(&at, 0); w(&at, -2); w(&at, 0); b(&at, 5); b(&at, 7);
        w(&at, 1); b(&at, 5); b(&at, 7);
        b(&at, 0x41); b(&at, 1); b(&at, 0); b(&at, 0x21); b(&at, 2); b(&at, 7); b(&at, 255);
        const int32_t xyz[2][3] = { {-10,20,30}, {40,-50,60} };
        for (int i = 0; i < 2; i++) {
            for (int j = 0; j < 3; j++) put(ds, 0x4EE6 + 16u * (i ? 7 : 5) + 4u * j, (uint32_t)xyz[i][j], 4);
            put(ds, 0x4EE6 + 16u * (i ? 7 : 5) + 12, 0xBAD0BAD0, 4);
        }
    } else {
        w(&at, 2); b(&at, 3); b(&at, 1); w(&at, 10); w(&at, 20); w(&at, 30);
        w(&at, 2); w(&at, ordered ? -1 : 1); w(&at, 0x8001); w(&at, 0x8100); b(&at, 0x21); b(&at, 255);
        w(&at, 1); w(&at, 0); w(&at, 0); w(&at, 0); w(&at, 0);
        b(&at, 2); b(&at, 0); b(&at, 1);
        if (ordered) { b(&at, 0); b(&at, 255); b(&at, 255); }
        w(&at, 1); w(&at, 2); w(&at, 3); w(&at, 0x8001); w(&at, -2); w(&at, 4); w(&at, 8);
        w(&at, 1); b(&at, 0); b(&at, 1); w(&at, 0x500);
        static const uint8_t material[] = {1,0,2,0x41,3,0,0x21,4,1,0x81,255,1,0,255,255};
        memcpy(bank + 0x500, material, sizeof material);
    }
    put(ds, 0x7D50, 0x1000, 2); put(ds, 0x7D4E, 0x9000, 2); put(ds, 0x9002, 65535, 2);
    put(ds, 0x7D80, 0x9100, 2); ds[0x7D8A] = 37;
    for (unsigned k = 0; k < 256; k++) put(ds, 0x9100 + 2 * k, 0xA000 + k, 2);
    for (int k = 0; k < 3; k++) {
        put(ds, 0x1000 + 8u * k, 32767, 2); put(ds, 0x1024 + 8u * k, (unsigned)k + 1, 2);
        put(ds, 0x7CDE + 4u * k, 100u * (k + 1), 4); put(ds, 0x7CEA + 4u * k, (uint32_t)(-10 * (k + 1)), 4);
        put(ds, 0x7CD2 + 4u * k, (uint32_t)(-5 - k), 4);
    }
    put(ds, 0x7C72, (uint16_t)-2, 2); put(ds, 0x7CA2, 16384, 2);
}
static int collect(void *user, const scene_material *c)
{ (void)user; if (nemitted >= 32) return 0; emitted[nemitted++] = *c; return 1; }
static int read_page(const char *path, uint8_t *page)
{
    FILE *f = fopen(path, "rb"); if (!f) return 0;
    const size_t n = fread(page, 1, SCENE_SEGMENT, f); const int extra = fgetc(f); fclose(f);
    return n == SCENE_SEGMENT && extra == EOF;
}
static int dump_material(void *user, const scene_material *c)
{
    int *first = (int *)user; printf("%s[%u,%u,%u,%u,%u,%u,[", *first ? "" : ",", c->offset, c->face, c->kind,
                                  c->colour_index, c->colour_word, c->shade); *first = 0;
    for (unsigned k = 0; k < c->count; k++) printf("%s%u", k ? "," : "", c->item[k]);
    printf("]]"); return 1;
}
static int dump(scene_mesh *m)
{
    uint16_t order[SCENE_PLANES]; unsigned count;
    CHECK(scene_pose_read(&pose, m, ds, sizeof ds)); CHECK(scene_mesh_eval(m, &pose, &geometry));
    CHECK(scene_mesh_order(m, &geometry, order, SCENE_PLANES, &count));
    printf("{\"lod\":%u,\"raw\":[%u,%u,%u],\"prefix\":[%u,%u,%u],\"pointers\":[%u,%u,%u],\"vertices\":[",
           m->lod,m->raw_parts,m->raw_vertices,m->raw_planes,m->prefix[0],m->prefix[1],m->prefix[2],m->pointers[0],m->pointers[1],m->pointers[2]);
    for (unsigned k=0;k<m->nvertices;k++) {
        const scene_vertex *v=&m->vertex[k];
        printf("%s[%u,%u,%d,[",k?",":"",v->offset,v->part,v->shared?(int)v->shared_index:-1);
        for(int j=0;j<3;j++)printf("%s%" PRId32,j?",":"",v->shared?pose.shared_camera[v->shared_index][j]:(int32_t)v->xyz[j]);
        printf("]]");
    }
    printf("],\"edges\":[");for(unsigned k=0;k<m->nedges;k++)printf("%s[%u,%u]",k?",":"",m->edge[k].a,m->edge[k].b);
    printf("],\"camera\":[");int first=1;
    for(unsigned k=0;k<SCENE_VERTICES;k++)if(geometry.present[k]) {
        printf("%s[%u,%" PRId32 ",%" PRId32 ",%" PRId32 ",%u]",first?"":",",k,geometry.camera[k][0],geometry.camera[k][1],geometry.camera[k][2],geometry.projected[k]);first=0;
    }
    printf("],\"visibility\":[");for(unsigned k=0;k<m->nplanes;k++)printf("%s%u",k?",":"",geometry.visibility[k]);
    printf("],\"shades\":[");for(unsigned k=0;k<m->nplanes;k++)printf("%s%u",k?",":"",geometry.shade[k]);
    printf("],\"face_order\":[");for(unsigned k=0;k<count;k++)printf("%s%u",k?",":"",order[k]);
    printf("],\"materials\":[");first=1;
    CHECK(scene_mesh_materials(m,&pose,&geometry,dump_material,&first));printf("]}\n");return 0;
}

int main(int argc, char **argv)
{
    scene_error error; scene_mesh *m;
    if (argc == 5) {
        CHECK(read_page(argv[1],bank));CHECK(read_page(argv[2],ds));
        m=scene_mesh_decode(bank,sizeof bank,(uint16_t)strtoul(argv[3],NULL,0),(uint16_t)strtoul(argv[4],NULL,0),&error);
        if(!m){fprintf(stderr,"%s\n",scene_error_name(error));return 1;}
        const int rc=dump(m);scene_mesh_free(m);return rc;
    }
    CHECK(argc==1);
    const int16_t camera[9]={32767,0,0,0,32767,0,0,0,32767};int16_t position[3]={0,0,12};
    CHECK(scene_model_depth(position,camera)==5);position[2]=14;CHECK(scene_model_depth(position,camera)==6);
    position[2]=-14;CHECK(scene_model_depth(position,camera)==7);
    fixture(0,0);m=scene_mesh_decode(bank,sizeof bank,0,5,&error);CHECK(m&&error==SCENE_OK);
    CHECK(m->nvertices==2&&m->nparts==2&&m->nplanes==1&&m->nedges==1);
    CHECK(m->part[1].angle==3&&m->part[1].axis==1&&m->part[1].offset[2]==30);
    CHECK(m->condition[0].setting==0x21&&m->condition[0].expected==-1);
    CHECK(m->vertex[1].part==1&&m->vertex[1].xyz[0]==-2);
    CHECK(scene_pose_read(&pose,m,ds,sizeof ds)&&scene_mesh_eval(m,&pose,&geometry));
    CHECK(geometry.camera[0][0]==32867&&geometry.camera[0][1]==65734&&geometry.camera[0][2]==98601);
    CHECK(geometry.camera[1][0]==-12&&geometry.camera[1][1]==-12&&geometry.camera[1][2]==-6);
    CHECK(geometry.present[0]&&geometry.present[1]&&!geometry.projected[0]&&!geometry.projected[1]);
    CHECK(geometry.visibility[0]==0&&geometry.shade[0]==1);
    pose.ortho=21;CHECK(scene_mesh_eval(m,&pose,&geometry));CHECK(geometry.visibility[0]==255&&geometry.projected[0]&&geometry.projected[1]);
    nemitted=0;CHECK(scene_mesh_materials(m,&pose,&geometry,collect,NULL));CHECK(nemitted==3);
    CHECK(emitted[0].kind==SCENE_POLYGON&&emitted[1].kind==SCENE_LINES&&emitted[2].kind==SCENE_DOTS);
    CHECK(emitted[0].colour_word==0xA002&&emitted[0].shade==1&&emitted[2].item[0]==1);
    pose.setting[1]=0;CHECK(scene_mesh_eval(m,&pose,&geometry));CHECK(geometry.visibility[0]==127&&!geometry.projected[0]);
    nemitted=0;CHECK(scene_mesh_materials(m,&pose,&geometry,collect,NULL)&&!nemitted);
    /* Mesh and pose must survive mutation/reuse of captured input storage. */
    memset(bank,0,sizeof bank);memset(ds,0,sizeof ds);CHECK(scene_mesh_eval(m,&pose,&geometry)&&geometry.camera[0][0]==32867);
    pose.nparts=1;CHECK(!scene_mesh_eval(m,&pose,&geometry));scene_mesh_free(m);
    fixture(1,0);m=scene_mesh_decode(bank,sizeof bank,0,0,&error);CHECK(m&&m->raw_vertices==65534);
    CHECK(scene_pose_read(&pose,m,ds,sizeof ds)&&scene_mesh_eval(m,&pose,&geometry));
    CHECK(geometry.camera[5][0]==-15&&geometry.camera[7][1]==-56&&geometry.projected[5]&&geometry.projected[7]);
    nemitted=0;CHECK(scene_mesh_materials(m,&pose,&geometry,collect,NULL)&&nemitted==2&&emitted[0].shade==37);
    pose.shared_present[5]=0;CHECK(!scene_mesh_eval(m,&pose,&geometry));scene_mesh_free(m);
    fixture(0,0);m=scene_mesh_decode(bank,sizeof bank,0,6,&error);CHECK(m&&!m->nvertices&&!m->nplanes);
    CHECK(scene_pose_read(&pose,m,ds,sizeof ds)&&scene_mesh_eval(m,&pose,&geometry));
    nemitted=0;CHECK(scene_mesh_materials(m,&pose,&geometry,collect,NULL)&&!nemitted);scene_mesh_free(m);
    fixture(0,1);m=scene_mesh_decode(bank,sizeof bank,0,0,&error);CHECK(m);
    /* A three-plane synthetic tree: back, self, front when nonzero;
     * a 7F conditional state is nonzero but its face is not painted. */
    m->nplanes=m->nfaces=3;m->raw_planes=(uint16_t)-3;
    const uint8_t tree[]={0,1,2,255,255,255,255};memcpy(m->order,tree,sizeof tree);
    memset(&geometry,0,sizeof geometry);geometry.visibility[0]=geometry.visibility[1]=geometry.visibility[2]=255;
    uint16_t order[4]={0};unsigned count;
    CHECK(scene_mesh_order(m,&geometry,order,4,&count)&&count==3&&order[0]==2&&order[1]==0&&order[2]==1);
    geometry.visibility[0]=0;CHECK(scene_mesh_order(m,&geometry,order,4,&count)&&count==2&&order[0]==1&&order[1]==2);
    geometry.visibility[0]=127;CHECK(scene_mesh_order(m,&geometry,order,4,&count)&&count==2&&order[0]==2&&order[1]==1);
    order[0]=0xABCD;CHECK(!scene_mesh_order(m,&geometry,order,1,&count)&&count==0&&order[0]==0xABCD);
    m->order[3]=0;CHECK(!scene_mesh_order(m,&geometry,order,4,&count)&&count==0&&order[0]==0xABCD);scene_mesh_free(m);
    fixture(0,0);CHECK(!scene_mesh_decode(bank,4,0,0,&error)&&error==SCENE_INPUT);
    CHECK(!scene_mesh_decode(bank,sizeof bank,1,0,&error)&&error==SCENE_INPUT);
    bank[0x501]=1;CHECK(!scene_mesh_decode(bank,sizeof bank,0,0,&error)&&error==SCENE_FORMAT);
    fixture(0,0);memset(bank+0x100,0,sizeof bank-0x100);CHECK(!scene_mesh_decode(bank,sizeof bank,0,32768,&error)&&error==SCENE_FORMAT);
    puts("native scene: complete topology, hidden geometry, prepared poses, shading, ordering, conditions and bounded failure pass");return 0;
}
