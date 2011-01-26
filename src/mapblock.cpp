/*
Minetest-c55
Copyright (C) 2010 celeron55, Perttu Ahola <celeron55@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "mapblock.h"
#include "map.h"
// For g_materials
#include "main.h"
#include "light.h"
#include <sstream>


/*
	MapBlock
*/

MapBlock::MapBlock(NodeContainer *parent, v3s16 pos, bool dummy):
		m_parent(parent),
		m_pos(pos),
		changed(true),
		is_underground(false),
		m_day_night_differs(false),
		m_objects(this)
{
	data = NULL;
	if(dummy == false)
		reallocate();
	
	m_spawn_timer = -10000;

#ifndef SERVER
	m_mesh_expired = false;
	mesh_mutex.Init();
	mesh = NULL;
	m_temp_mods_mutex.Init();
#endif
}

MapBlock::~MapBlock()
{
#ifndef SERVER
	{
		JMutexAutoLock lock(mesh_mutex);
		
		if(mesh)
		{
			mesh->drop();
			mesh = NULL;
		}
	}
#endif

	if(data)
		delete[] data;
}

bool MapBlock::isValidPositionParent(v3s16 p)
{
	if(isValidPosition(p))
	{
		return true;
	}
	else{
		return m_parent->isValidPosition(getPosRelative() + p);
	}
}

MapNode MapBlock::getNodeParent(v3s16 p)
{
	if(isValidPosition(p) == false)
	{
		return m_parent->getNode(getPosRelative() + p);
	}
	else
	{
		if(data == NULL)
			throw InvalidPositionException();
		return data[p.Z*MAP_BLOCKSIZE*MAP_BLOCKSIZE + p.Y*MAP_BLOCKSIZE + p.X];
	}
}

void MapBlock::setNodeParent(v3s16 p, MapNode & n)
{
	if(isValidPosition(p) == false)
	{
		m_parent->setNode(getPosRelative() + p, n);
	}
	else
	{
		if(data == NULL)
			throw InvalidPositionException();
		data[p.Z*MAP_BLOCKSIZE*MAP_BLOCKSIZE + p.Y*MAP_BLOCKSIZE + p.X] = n;
	}
}

MapNode MapBlock::getNodeParentNoEx(v3s16 p)
{
	if(isValidPosition(p) == false)
	{
		try{
			return m_parent->getNode(getPosRelative() + p);
		}
		catch(InvalidPositionException &e)
		{
			return MapNode(CONTENT_IGNORE);
		}
	}
	else
	{
		if(data == NULL)
		{
			return MapNode(CONTENT_IGNORE);
		}
		return data[p.Z*MAP_BLOCKSIZE*MAP_BLOCKSIZE + p.Y*MAP_BLOCKSIZE + p.X];
	}
}

/*
	Parameters must consist of air and !air.
	Order doesn't matter.

	If either of the nodes doesn't exist, light is 0.
	
	parameters:
		daynight_ratio: 0...1000
		n: getNodeParent(p)
		n2: getNodeParent(p + face_dir)
		face_dir: axis oriented unit vector from p to p2
	
	returns encoded light value.
*/
u8 MapBlock::getFaceLight(u32 daynight_ratio, MapNode n, MapNode n2,
		v3s16 face_dir)
{
	try{
		// DEBUG
		/*{
			if(n.d == CONTENT_WATER)
			{
				u8 l = n.param2*2;
				if(l > LIGHT_MAX)
					l = LIGHT_MAX;
				return l;
			}
			if(n2.d == CONTENT_WATER)
			{
				u8 l = n2.param2*2;
				if(l > LIGHT_MAX)
					l = LIGHT_MAX;
				return l;
			}
		}*/


		u8 light;
		u8 l1 = n.getLightBlend(daynight_ratio);
		u8 l2 = n2.getLightBlend(daynight_ratio);
		if(l1 > l2)
			light = l1;
		else
			light = l2;

		// Make some nice difference to different sides

		/*if(face_dir.X == 1 || face_dir.Z == 1 || face_dir.Y == -1)
			light = diminish_light(diminish_light(light));
		else if(face_dir.X == -1 || face_dir.Z == -1)
			light = diminish_light(light);*/

		if(face_dir.X == 1 || face_dir.X == -1 || face_dir.Y == -1)
			light = diminish_light(diminish_light(light));
		else if(face_dir.Z == 1 || face_dir.Z == -1)
			light = diminish_light(light);

		return light;
	}
	catch(InvalidPositionException &e)
	{
		return 0;
	}
}

#ifndef SERVER

void MapBlock::makeFastFace(TileSpec tile, u8 light, v3f p,
		v3s16 dir, v3f scale, v3f posRelative_f,
		core::array<FastFace> &dest)
{
	FastFace face;
	
	// Position is at the center of the cube.
	v3f pos = p * BS;
	posRelative_f *= BS;

	v3f vertex_pos[4];
	// If looking towards z+, this is the face that is behind
	// the center point, facing towards z+.
	vertex_pos[0] = v3f( BS/2,-BS/2,BS/2);
	vertex_pos[1] = v3f(-BS/2,-BS/2,BS/2);
	vertex_pos[2] = v3f(-BS/2, BS/2,BS/2);
	vertex_pos[3] = v3f( BS/2, BS/2,BS/2);
	
	if(dir == v3s16(0,0,1))
	{
		for(u16 i=0; i<4; i++)
			vertex_pos[i].rotateXZBy(0);
	}
	else if(dir == v3s16(0,0,-1))
	{
		for(u16 i=0; i<4; i++)
			vertex_pos[i].rotateXZBy(180);
	}
	else if(dir == v3s16(1,0,0))
	{
		for(u16 i=0; i<4; i++)
			vertex_pos[i].rotateXZBy(-90);
	}
	else if(dir == v3s16(-1,0,0))
	{
		for(u16 i=0; i<4; i++)
			vertex_pos[i].rotateXZBy(90);
	}
	else if(dir == v3s16(0,1,0))
	{
		for(u16 i=0; i<4; i++)
			vertex_pos[i].rotateYZBy(-90);
	}
	else if(dir == v3s16(0,-1,0))
	{
		for(u16 i=0; i<4; i++)
			vertex_pos[i].rotateYZBy(90);
	}

	for(u16 i=0; i<4; i++)
	{
		vertex_pos[i].X *= scale.X;
		vertex_pos[i].Y *= scale.Y;
		vertex_pos[i].Z *= scale.Z;
		vertex_pos[i] += pos + posRelative_f;
	}

	f32 abs_scale = 1.;
	if     (scale.X < 0.999 || scale.X > 1.001) abs_scale = scale.X;
	else if(scale.Y < 0.999 || scale.Y > 1.001) abs_scale = scale.Y;
	else if(scale.Z < 0.999 || scale.Z > 1.001) abs_scale = scale.Z;

	v3f zerovector = v3f(0,0,0);
	
	//u8 li = decode_light(light);
	u8 li = light;

	u8 alpha = tile.alpha;
	/*u8 alpha = 255;
	if(tile.id == TILE_WATER)
		alpha = WATER_ALPHA;*/

	video::SColor c = video::SColor(alpha,li,li,li);

	face.vertices[0] = video::S3DVertex(vertex_pos[0], zerovector, c,
			core::vector2d<f32>(0,1));
	face.vertices[1] = video::S3DVertex(vertex_pos[1], zerovector, c,
			core::vector2d<f32>(abs_scale,1));
	face.vertices[2] = video::S3DVertex(vertex_pos[2], zerovector, c,
			core::vector2d<f32>(abs_scale,0));
	face.vertices[3] = video::S3DVertex(vertex_pos[3], zerovector, c,
			core::vector2d<f32>(0,0));

	face.tile = tile;
	//DEBUG
	//f->tile = TILE_STONE;
	
	dest.push_back(face);
	//return f;
}
	
/*
	Gets node tile from any place relative to block.
	Returns TILE_NODE if doesn't exist or should not be drawn.
*/
TileSpec MapBlock::getNodeTile(MapNode mn, v3s16 p, v3s16 face_dir)
{
	TileSpec spec;
	spec = mn.getTile(face_dir);
	
	/*
		Check temporary modifications on this node
	*/
	core::map<v3s16, NodeMod>::Node *n;
	n = m_temp_mods.find(p);

	// If modified
	if(n != NULL)
	{
		struct NodeMod mod = n->getValue();
		if(mod.type == NODEMOD_CHANGECONTENT)
		{
			//spec = content_tile(mod.param, face_dir);
			MapNode mn2(mod.param);
			spec = mn2.getTile(face_dir);
		}
		if(mod.type == NODEMOD_CRACK)
		{
			std::ostringstream os;
			os<<"[[mod:crack"<<mod.param;
			spec.name += os.str();
		}
	}
	
	return spec;
}

u8 MapBlock::getNodeContent(v3s16 p, MapNode mn)
{
	/*
		Check temporary modifications on this node
	*/
	core::map<v3s16, NodeMod>::Node *n;
	n = m_temp_mods.find(p);

	// If modified
	if(n != NULL)
	{
		struct NodeMod mod = n->getValue();
		if(mod.type == NODEMOD_CHANGECONTENT)
		{
			// Overrides content
			return mod.param;
		}
		if(mod.type == NODEMOD_CRACK)
		{
			/*
				Content doesn't change.
				
				face_contents works just like it should, because
				there should not be faces between differently cracked
				nodes.

				If a semi-transparent node is cracked in front an
				another one, it really doesn't matter whether there
				is a cracked face drawn in between or not.
			*/
		}
	}

	return mn.d;
}

/*
	startpos:
	translate_dir: unit vector with only one of x, y or z
	face_dir: unit vector with only one of x, y or z
*/
void MapBlock::updateFastFaceRow(
		u32 daynight_ratio,
		v3f posRelative_f,
		v3s16 startpos,
		u16 length,
		v3s16 translate_dir,
		v3f translate_dir_f,
		v3s16 face_dir,
		v3f face_dir_f,
		core::array<FastFace> &dest)
{
	v3s16 p = startpos;
	
	u16 continuous_tiles_count = 0;
	
	MapNode n0 = getNodeParentNoEx(p);
	MapNode n1 = getNodeParentNoEx(p + face_dir);

	u8 light = getFaceLight(daynight_ratio, n0, n1, face_dir);
		
	TileSpec tile0 = getNodeTile(n0, p, face_dir);
	TileSpec tile1 = getNodeTile(n1, p + face_dir, -face_dir);

	for(u16 j=0; j<length; j++)
	{
		bool next_is_different = true;
		
		v3s16 p_next;
		MapNode n0_next;
		MapNode n1_next;
		TileSpec tile0_next;
		TileSpec tile1_next;
		u8 light_next = 0;

		if(j != length - 1)
		{
			p_next = p + translate_dir;
			n0_next = getNodeParentNoEx(p_next);
			n1_next = getNodeParentNoEx(p_next + face_dir);
			tile0_next = getNodeTile(n0_next, p_next, face_dir);
			tile1_next = getNodeTile(n1_next, p_next + face_dir, -face_dir);
			light_next = getFaceLight(daynight_ratio, n0_next, n1_next, face_dir);

			if(tile0_next == tile0
					&& tile1_next == tile1
					&& light_next == light)
			{
				next_is_different = false;
			}
		}

		continuous_tiles_count++;
		
		if(next_is_different)
		{
			/*
				Create a face if there should be one
			*/
			//u8 mf = face_contents(tile0, tile1);
			// This is hackish
			u8 content0 = getNodeContent(p, n0);
			u8 content1 = getNodeContent(p + face_dir, n1);
			u8 mf = face_contents(content0, content1);
			
			if(mf != 0)
			{
				// Floating point conversion of the position vector
				v3f pf(p.X, p.Y, p.Z);
				// Center point of face (kind of)
				v3f sp = pf - ((f32)continuous_tiles_count / 2. - 0.5) * translate_dir_f;
				v3f scale(1,1,1);
				if(translate_dir.X != 0){
					scale.X = continuous_tiles_count;
				}
				if(translate_dir.Y != 0){
					scale.Y = continuous_tiles_count;
				}
				if(translate_dir.Z != 0){
					scale.Z = continuous_tiles_count;
				}
				
				//FastFace *f;

				// If node at sp (tile0) is more solid
				if(mf == 1)
				{
					makeFastFace(tile0, decode_light(light),
							sp, face_dir, scale,
							posRelative_f, dest);
				}
				// If node at sp is less solid (mf == 2)
				else
				{
					makeFastFace(tile1, decode_light(light),
							sp+face_dir_f, -face_dir, scale,
							posRelative_f, dest);
				}
				//dest.push_back(f);
			}

			continuous_tiles_count = 0;
			n0 = n0_next;
			n1 = n1_next;
			tile0 = tile0_next;
			tile1 = tile1_next;
			light = light_next;
		}
		
		p = p_next;
	}
}

/*
	This is used because CMeshBuffer::append() is very slow
*/
struct PreMeshBuffer
{
	video::SMaterial material;
	core::array<u16> indices;
	core::array<video::S3DVertex> vertices;
};

class MeshCollector
{
public:
	void append(
			video::SMaterial material,
			const video::S3DVertex* const vertices,
			u32 numVertices,
			const u16* const indices,
			u32 numIndices
		)
	{
		PreMeshBuffer *p = NULL;
		for(u32 i=0; i<m_prebuffers.size(); i++)
		{
			PreMeshBuffer &pp = m_prebuffers[i];
			if(pp.material != material)
				continue;

			p = &pp;
			break;
		}

		if(p == NULL)
		{
			PreMeshBuffer pp;
			pp.material = material;
			m_prebuffers.push_back(pp);
			p = &m_prebuffers[m_prebuffers.size()-1];
		}

		u32 vertex_count = p->vertices.size();
		for(u32 i=0; i<numIndices; i++)
		{
			u32 j = indices[i] + vertex_count;
			if(j > 65535)
			{
				dstream<<"FIXME: Meshbuffer ran out of indices"<<std::endl;
				// NOTE: Fix is to just add an another MeshBuffer
			}
			p->indices.push_back(j);
		}
		for(u32 i=0; i<numVertices; i++)
		{
			p->vertices.push_back(vertices[i]);
		}
	}

	void fillMesh(scene::SMesh *mesh)
	{
		/*dstream<<"Filling mesh with "<<m_prebuffers.size()
				<<" meshbuffers"<<std::endl;*/
		for(u32 i=0; i<m_prebuffers.size(); i++)
		{
			PreMeshBuffer &p = m_prebuffers[i];

			/*dstream<<"p.vertices.size()="<<p.vertices.size()
					<<", p.indices.size()="<<p.indices.size()
					<<std::endl;*/
			
			// Create meshbuffer
			
			// This is a "Standard MeshBuffer",
			// it's a typedeffed CMeshBuffer<video::S3DVertex>
			scene::SMeshBuffer *buf = new scene::SMeshBuffer();
			// Set material
			buf->Material = p.material;
			//((scene::SMeshBuffer*)buf)->Material = p.material;
			// Use VBO
			//buf->setHardwareMappingHint(scene::EHM_STATIC);
			// Add to mesh
			mesh->addMeshBuffer(buf);
			// Mesh grabbed it
			buf->drop();

			buf->append(p.vertices.pointer(), p.vertices.size(),
					p.indices.pointer(), p.indices.size());
		}
	}

private:
	core::array<PreMeshBuffer> m_prebuffers;
};

void MapBlock::updateMesh(u32 daynight_ratio)
{
#if 0
	/*
		DEBUG: If mesh has been generated, don't generate it again
	*/
	{
		JMutexAutoLock meshlock(mesh_mutex);
		if(mesh != NULL)
			return;
	}
#endif
	
	// 4-21ms for MAP_BLOCKSIZE=16
	// 24-155ms for MAP_BLOCKSIZE=32
	//TimeTaker timer1("updateMesh()");

	core::array<FastFace> fastfaces_new;
	
	v3f posRelative_f(getPosRelative().X, getPosRelative().Y,
			getPosRelative().Z); // floating point conversion
	
	/*
		We are including the faces of the trailing edges of the block.
		This means that when something changes, the caller must
		also update the meshes of the blocks at the leading edges.

		NOTE: This is the slowest part of this method.
	*/
	
	{
		TimeTaker timer2("updateMesh() collect");

		// Lock this, as m_temp_mods will be used directly
		JMutexAutoLock lock(m_temp_mods_mutex);

		/*
			Go through every y,z and get top faces in rows of x+
		*/
		for(s16 y=0; y<MAP_BLOCKSIZE; y++){
			for(s16 z=0; z<MAP_BLOCKSIZE; z++){
				updateFastFaceRow(daynight_ratio, posRelative_f,
						v3s16(0,y,z), MAP_BLOCKSIZE,
						v3s16(1,0,0), //dir
						v3f  (1,0,0),
						v3s16(0,1,0), //face dir
						v3f  (0,1,0),
						fastfaces_new);
			}
		}
		/*
			Go through every x,y and get right faces in rows of z+
		*/
		for(s16 x=0; x<MAP_BLOCKSIZE; x++){
			for(s16 y=0; y<MAP_BLOCKSIZE; y++){
				updateFastFaceRow(daynight_ratio, posRelative_f,
						v3s16(x,y,0), MAP_BLOCKSIZE,
						v3s16(0,0,1),
						v3f  (0,0,1),
						v3s16(1,0,0),
						v3f  (1,0,0),
						fastfaces_new);
			}
		}
		/*
			Go through every y,z and get back faces in rows of x+
		*/
		for(s16 z=0; z<MAP_BLOCKSIZE; z++){
			for(s16 y=0; y<MAP_BLOCKSIZE; y++){
				updateFastFaceRow(daynight_ratio, posRelative_f,
						v3s16(0,y,z), MAP_BLOCKSIZE,
						v3s16(1,0,0),
						v3f  (1,0,0),
						v3s16(0,0,1),
						v3f  (0,0,1),
						fastfaces_new);
			}
		}
	}

	// End of slow part

	/*
		Convert FastFaces to SMesh
	*/

	scene::SMesh *mesh_new = NULL;
	
	mesh_new = new scene::SMesh();
	
	MeshCollector collector;

	if(fastfaces_new.size() > 0)
	{
		// avg 0ms (100ms spikes when loading textures the first time)
		//TimeTaker timer2("updateMesh() mesh building");

		for(u32 i=0; i<fastfaces_new.size(); i++)
		{
			FastFace &f = fastfaces_new[i];

			const u16 indices[] = {0,1,2,2,3,0};

			video::ITexture *texture = g_irrlicht->getTexture(f.tile.name);
			video::SMaterial material;
			material.Lighting = false;
			material.BackfaceCulling = false;
			material.setFlag(video::EMF_BILINEAR_FILTER, false);
			material.setFlag(video::EMF_ANTI_ALIASING, video::EAAM_OFF);
			material.setFlag(video::EMF_FOG_ENABLE, true);
			material.setTexture(0, texture);
			if(f.tile.alpha != 255)
				material.MaterialType = video::EMT_TRANSPARENT_VERTEX_ALPHA;
			
			collector.append(material, f.vertices, 4, indices, 6);
		}
	}

	/*
		Add special graphics:
		- torches
		
		TODO: Optimize by using same meshbuffer for same textures
	*/

	// 0ms
	//TimeTaker timer2("updateMesh() adding special stuff");

	for(s16 z=0; z<MAP_BLOCKSIZE; z++)
	for(s16 y=0; y<MAP_BLOCKSIZE; y++)
	for(s16 x=0; x<MAP_BLOCKSIZE; x++)
	{
		v3s16 p(x,y,z);

		MapNode &n = getNodeRef(x,y,z);
		
		/*
			Add torches to mesh
		*/
		if(n.d == CONTENT_TORCH)
		{
			video::SColor c(255,255,255,255);

			video::S3DVertex vertices[4] =
			{
				video::S3DVertex(-BS/2,-BS/2,0, 0,0,0, c, 0,1),
				video::S3DVertex(BS/2,-BS/2,0, 0,0,0, c, 1,1),
				video::S3DVertex(BS/2,BS/2,0, 0,0,0, c, 1,0),
				video::S3DVertex(-BS/2,BS/2,0, 0,0,0, c, 0,0),
			};

			v3s16 dir = unpackDir(n.dir);

			for(s32 i=0; i<4; i++)
			{
				if(dir == v3s16(1,0,0))
					vertices[i].Pos.rotateXZBy(0);
				if(dir == v3s16(-1,0,0))
					vertices[i].Pos.rotateXZBy(180);
				if(dir == v3s16(0,0,1))
					vertices[i].Pos.rotateXZBy(90);
				if(dir == v3s16(0,0,-1))
					vertices[i].Pos.rotateXZBy(-90);
				if(dir == v3s16(0,-1,0))
					vertices[i].Pos.rotateXZBy(45);
				if(dir == v3s16(0,1,0))
					vertices[i].Pos.rotateXZBy(-45);

				vertices[i].Pos += intToFloat(p + getPosRelative());
			}

			// Set material
			video::SMaterial material;
			material.setFlag(video::EMF_LIGHTING, false);
			material.setFlag(video::EMF_BACK_FACE_CULLING, false);
			material.setFlag(video::EMF_BILINEAR_FILTER, false);
			//material.MaterialType = video::EMT_TRANSPARENT_ALPHA_CHANNEL;
			material.MaterialType
					= video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF;
			if(dir == v3s16(0,-1,0))
				material.setTexture(0,
						g_irrlicht->getTexture(porting::getDataPath("torch_on_floor.png").c_str()));
			else if(dir == v3s16(0,1,0))
				material.setTexture(0,
						g_irrlicht->getTexture(porting::getDataPath("torch_on_ceiling.png").c_str()));
			// For backwards compatibility
			else if(dir == v3s16(0,0,0))
				material.setTexture(0,
						g_irrlicht->getTexture(porting::getDataPath("torch_on_floor.png").c_str()));
			else
				material.setTexture(0, 
						g_irrlicht->getTexture(porting::getDataPath("torch.png").c_str()));

			u16 indices[] = {0,1,2,2,3,0};
			// Add to mesh collector
			collector.append(material, vertices, 4, indices, 6);
		}
		/*
			Add flowing water to mesh
		*/
		else if(n.d == CONTENT_WATER)
		{
			bool top_is_water = false;
			try{
				MapNode n = getNodeParent(v3s16(x,y+1,z));
				if(n.d == CONTENT_WATER || n.d == CONTENT_WATERSOURCE)
					top_is_water = true;
			}catch(InvalidPositionException &e){}
			
			u8 l = decode_light(n.getLightBlend(daynight_ratio));
			video::SColor c(WATER_ALPHA,l,l,l);
			
			// Neighbor water levels (key = relative position)
			// Includes current node
			core::map<v3s16, f32> neighbor_levels;
			core::map<v3s16, u8> neighbor_contents;
			v3s16 neighbor_dirs[9] = {
				v3s16(0,0,0),
				v3s16(0,0,1),
				v3s16(0,0,-1),
				v3s16(1,0,0),
				v3s16(-1,0,0),
				v3s16(1,0,1),
				v3s16(-1,0,-1),
				v3s16(1,0,-1),
				v3s16(-1,0,1),
			};
			for(u32 i=0; i<9; i++)
			{
				u8 content = CONTENT_AIR;
				float level = -0.5 * BS;
				try{
					v3s16 p2 = p + neighbor_dirs[i];
					MapNode n2 = getNodeParent(p2);

					content = n2.d;

					if(n2.d == CONTENT_WATERSOURCE)
						level = 0.5 * BS;
					else if(n2.d == CONTENT_WATER)
						level = (-0.5 + ((float)n2.param2 + 0.5) / 8.0) * BS;
				}
				catch(InvalidPositionException &e){}
				
				neighbor_levels.insert(neighbor_dirs[i], level);
				neighbor_contents.insert(neighbor_dirs[i], content);
			}

			//float water_level = (-0.5 + ((float)n.param2 + 0.5) / 8.0) * BS;
			//float water_level = neighbor_levels[v3s16(0,0,0)];

			// Corner heights (average between four waters)
			f32 corner_levels[4];
			
			v3s16 halfdirs[4] = {
				v3s16(0,0,0),
				v3s16(1,0,0),
				v3s16(1,0,1),
				v3s16(0,0,1),
			};
			for(u32 i=0; i<4; i++)
			{
				v3s16 cornerdir = halfdirs[i];
				float cornerlevel = 0;
				u32 valid_count = 0;
				for(u32 j=0; j<4; j++)
				{
					v3s16 neighbordir = cornerdir - halfdirs[j];
					u8 content = neighbor_contents[neighbordir];
					// Special case for source nodes
					if(content == CONTENT_WATERSOURCE)
					{
						cornerlevel = 0.5*BS;
						valid_count = 1;
						break;
					}
					else if(content == CONTENT_WATER)
					{
						cornerlevel += neighbor_levels[neighbordir];
						valid_count++;
					}
					else if(content == CONTENT_AIR)
					{
						cornerlevel += -0.5*BS;
						valid_count++;
					}
				}
				if(valid_count > 0)
					cornerlevel /= valid_count;
				corner_levels[i] = cornerlevel;
			}

			/*
				Generate sides
			*/

			v3s16 side_dirs[4] = {
				v3s16(1,0,0),
				v3s16(-1,0,0),
				v3s16(0,0,1),
				v3s16(0,0,-1),
			};
			s16 side_corners[4][2] = {
				{1, 2},
				{3, 0},
				{2, 3},
				{0, 1},
			};
			for(u32 i=0; i<4; i++)
			{
				v3s16 dir = side_dirs[i];

				//float neighbor_level = neighbor_levels[dir];
				/*if(neighbor_level > -0.5*BS + 0.001)
					continue;*/
				/*if(neighbor_level > water_level - 0.1*BS)
					continue;*/

				u8 neighbor_content = neighbor_contents[dir];

				if(neighbor_content != CONTENT_AIR
						&& neighbor_content != CONTENT_WATER)
					continue;
				
				bool neighbor_is_water = (neighbor_content == CONTENT_WATER);

				if(neighbor_is_water == true && top_is_water == false)
					continue;
				
				video::S3DVertex vertices[4] =
				{
					/*video::S3DVertex(-BS/2,-BS/2,BS/2, 0,0,0, c, 0,1),
					video::S3DVertex(BS/2,-BS/2,BS/2, 0,0,0, c, 1,1),
					video::S3DVertex(BS/2,BS/2,BS/2, 0,0,0, c, 1,0),
					video::S3DVertex(-BS/2,BS/2,BS/2, 0,0,0, c, 0,0),*/
					video::S3DVertex(-BS/2,0,BS/2, 0,0,0, c, 0,1),
					video::S3DVertex(BS/2,0,BS/2, 0,0,0, c, 1,1),
					video::S3DVertex(BS/2,0,BS/2, 0,0,0, c, 1,0),
					video::S3DVertex(-BS/2,0,BS/2, 0,0,0, c, 0,0),
				};
				
				if(top_is_water)
				{
					vertices[2].Pos.Y = 0.5*BS;
					vertices[3].Pos.Y = 0.5*BS;
				}
				else
				{
					vertices[2].Pos.Y = corner_levels[side_corners[i][0]];
					vertices[3].Pos.Y = corner_levels[side_corners[i][1]];
				}

				if(neighbor_is_water)
				{
					vertices[0].Pos.Y = corner_levels[side_corners[i][1]];
					vertices[1].Pos.Y = corner_levels[side_corners[i][0]];
				}
				else
				{
					vertices[0].Pos.Y = -0.5*BS;
					vertices[1].Pos.Y = -0.5*BS;
				}
				
				for(s32 j=0; j<4; j++)
				{
					if(dir == v3s16(0,0,1))
						vertices[j].Pos.rotateXZBy(0);
					if(dir == v3s16(0,0,-1))
						vertices[j].Pos.rotateXZBy(180);
					if(dir == v3s16(-1,0,0))
						vertices[j].Pos.rotateXZBy(90);
					if(dir == v3s16(1,0,-0))
						vertices[j].Pos.rotateXZBy(-90);

					vertices[j].Pos += intToFloat(p + getPosRelative());
				}

				// Set material
				video::SMaterial material;
				material.setFlag(video::EMF_LIGHTING, false);
				material.setFlag(video::EMF_BACK_FACE_CULLING, false);
				material.setFlag(video::EMF_BILINEAR_FILTER, false);
				material.setFlag(video::EMF_FOG_ENABLE, true);
				material.MaterialType = video::EMT_TRANSPARENT_VERTEX_ALPHA;
				material.setTexture(0,
						g_irrlicht->getTexture(porting::getDataPath("water.png").c_str()));

				u16 indices[] = {0,1,2,2,3,0};
				// Add to mesh collector
				collector.append(material, vertices, 4, indices, 6);
			}
			
			/*
				Generate top side, if appropriate
			*/
			
			if(top_is_water == false)
			{
				video::S3DVertex vertices[4] =
				{
					video::S3DVertex(-BS/2,0,-BS/2, 0,0,0, c, 0,1),
					video::S3DVertex(BS/2,0,-BS/2, 0,0,0, c, 1,1),
					video::S3DVertex(BS/2,0,BS/2, 0,0,0, c, 1,0),
					video::S3DVertex(-BS/2,0,BS/2, 0,0,0, c, 0,0),
				};

				for(s32 i=0; i<4; i++)
				{
					//vertices[i].Pos.Y += water_level;
					//vertices[i].Pos.Y += neighbor_levels[v3s16(0,0,0)];
					vertices[i].Pos.Y += corner_levels[i];
					vertices[i].Pos += intToFloat(p + getPosRelative());
				}

				// Set material
				video::SMaterial material;
				material.setFlag(video::EMF_LIGHTING, false);
				material.setFlag(video::EMF_BACK_FACE_CULLING, false);
				material.setFlag(video::EMF_BILINEAR_FILTER, false);
				material.setFlag(video::EMF_FOG_ENABLE, true);
				material.MaterialType = video::EMT_TRANSPARENT_VERTEX_ALPHA;
				material.setTexture(0,
						g_irrlicht->getTexture(porting::getDataPath("water.png").c_str()));

				u16 indices[] = {0,1,2,2,3,0};
				// Add to mesh collector
				collector.append(material, vertices, 4, indices, 6);
			}
		}
	}

	/*
		Add stuff from collector to mesh
	*/
	
	collector.fillMesh(mesh_new);

	/*
		Do some stuff to the mesh
	*/

	mesh_new->recalculateBoundingBox();

	/*
		Delete new mesh if it is empty
	*/

	if(mesh_new->getMeshBufferCount() == 0)
	{
		mesh_new->drop();
		mesh_new = NULL;
	}

	// Use VBO for mesh (this just would set this for ever buffer)
	// This will lead to infinite memory usage because or irrlicht.
	//mesh_new->setHardwareMappingHint(scene::EHM_STATIC);
	
	/*std::cout<<"MapBlock has "<<fastfaces_new.size()<<" faces "
			<<"and uses "<<mesh_new->getMeshBufferCount()
			<<" materials (meshbuffers)"<<std::endl;*/
	
	/*
		Replace the mesh
	*/

	mesh_mutex.Lock();

	//scene::SMesh *mesh_old = mesh[daynight_i];
	//mesh[daynight_i] = mesh_new;

	scene::SMesh *mesh_old = mesh;
	mesh = mesh_new;
	setMeshExpired(false);
	
	if(mesh_old != NULL)
	{
		// Remove hardware buffers of meshbuffers of mesh
		// NOTE: No way, this runs in a different thread and everything
		/*u32 c = mesh_old->getMeshBufferCount();
		for(u32 i=0; i<c; i++)
		{
			IMeshBuffer *buf = mesh_old->getMeshBuffer(i);
		}*/
		
		/*dstream<<"mesh_old->getReferenceCount()="
				<<mesh_old->getReferenceCount()<<std::endl;
		u32 c = mesh_old->getMeshBufferCount();
		for(u32 i=0; i<c; i++)
		{
			scene::IMeshBuffer *buf = mesh_old->getMeshBuffer(i);
			dstream<<"buf->getReferenceCount()="
					<<buf->getReferenceCount()<<std::endl;
		}*/

		// Drop the mesh
		mesh_old->drop();

		//delete mesh_old;
	}

	mesh_mutex.Unlock();
	
	//std::cout<<"added "<<fastfaces.getSize()<<" faces."<<std::endl;
}

/*void MapBlock::updateMeshes(s32 first_i)
{
	assert(first_i >= 0 && first_i <= DAYNIGHT_CACHE_COUNT);
	updateMesh(first_i);
	for(s32 i=0; i<DAYNIGHT_CACHE_COUNT; i++)
	{
		if(i == first_i)
			continue;
		updateMesh(i);
	}
}*/

#endif // !SERVER

/*
	Propagates sunlight down through the block.
	Doesn't modify nodes that are not affected by sunlight.
	
	Returns false if sunlight at bottom block is invalid
	Returns true if bottom block doesn't exist.

	If there is a block above, continues from it.
	If there is no block above, assumes there is sunlight, unless
	is_underground is set or highest node is water.

	At the moment, all sunlighted nodes are added to light_sources.
	- SUGG: This could be optimized

	Turns sunglighted mud into grass.

	if remove_light==true, sets non-sunlighted nodes black.

	if black_air_left!=NULL, it is set to true if non-sunlighted
	air is left in block.
*/
bool MapBlock::propagateSunlight(core::map<v3s16, bool> & light_sources,
		bool remove_light, bool *black_air_left,
		bool grow_grass)
{
	// Whether the sunlight at the top of the bottom block is valid
	bool block_below_is_valid = true;
	
	v3s16 pos_relative = getPosRelative();
	
	for(s16 x=0; x<MAP_BLOCKSIZE; x++)
	{
		for(s16 z=0; z<MAP_BLOCKSIZE; z++)
		{
#if 1
			bool no_sunlight = false;
			bool no_top_block = false;
			// Check if node above block has sunlight
			try{
				MapNode n = getNodeParent(v3s16(x, MAP_BLOCKSIZE, z));
				if(n.getLight(LIGHTBANK_DAY) != LIGHT_SUN)
				{
					no_sunlight = true;
				}
			}
			catch(InvalidPositionException &e)
			{
				no_top_block = true;
				
				// NOTE: This makes over-ground roofed places sunlighted
				// Assume sunlight, unless is_underground==true
				if(is_underground)
				{
					no_sunlight = true;
				}
				else
				{
					MapNode n = getNode(v3s16(x, MAP_BLOCKSIZE-1, z));
					if(n.d == CONTENT_WATER || n.d == CONTENT_WATERSOURCE)
					{
						no_sunlight = true;
					}
				}
				// NOTE: As of now, this just would make everything dark.
				// No sunlight here
				//no_sunlight = true;
			}
#endif
#if 0 // Doesn't work; nothing gets light.
			bool no_sunlight = true;
			bool no_top_block = false;
			// Check if node above block has sunlight
			try{
				MapNode n = getNodeParent(v3s16(x, MAP_BLOCKSIZE, z));
				if(n.getLight(LIGHTBANK_DAY) == LIGHT_SUN)
				{
					no_sunlight = false;
				}
			}
			catch(InvalidPositionException &e)
			{
				no_top_block = true;
			}
#endif

			/*std::cout<<"("<<x<<","<<z<<"): "
					<<"no_top_block="<<no_top_block
					<<", is_underground="<<is_underground
					<<", no_sunlight="<<no_sunlight
					<<std::endl;*/
		
			s16 y = MAP_BLOCKSIZE-1;
			
			// This makes difference to diminishing in water.
			bool stopped_to_solid_object = false;
			
			u8 current_light = no_sunlight ? 0 : LIGHT_SUN;

			for(; y >= 0; y--)
			{
				v3s16 pos(x, y, z);
				MapNode &n = getNodeRef(pos);
				
				if(current_light == 0)
				{
					// Do nothing
				}
				else if(current_light == LIGHT_SUN && n.sunlight_propagates())
				{
					// Do nothing: Sunlight is continued
				}
				else if(n.light_propagates() == false)
				{
					if(grow_grass)
					{
						bool upper_is_air = false;
						try
						{
							if(getNodeParent(pos+v3s16(0,1,0)).d == CONTENT_AIR)
								upper_is_air = true;
						}
						catch(InvalidPositionException &e)
						{
						}
						// Turn mud into grass
						if(upper_is_air && n.d == CONTENT_MUD
								&& current_light == LIGHT_SUN)
						{
							n.d = CONTENT_GRASS;
						}
					}

					// A solid object is on the way.
					stopped_to_solid_object = true;
					
					// Light stops.
					current_light = 0;
				}
				else
				{
					// Diminish light
					current_light = diminish_light(current_light);
				}

				u8 old_light = n.getLight(LIGHTBANK_DAY);

				if(current_light > old_light || remove_light)
				{
					n.setLight(LIGHTBANK_DAY, current_light);
				}
				
				if(diminish_light(current_light) != 0)
				{
					light_sources.insert(pos_relative + pos, true);
				}

				if(current_light == 0 && stopped_to_solid_object)
				{
					if(black_air_left)
					{
						*black_air_left = true;
					}
				}
			}

			// Whether or not the block below should see LIGHT_SUN
			bool sunlight_should_go_down = (current_light == LIGHT_SUN);

			/*
				If the block below hasn't already been marked invalid:

				Check if the node below the block has proper sunlight at top.
				If not, the block below is invalid.
				
				Ignore non-transparent nodes as they always have no light
			*/
			try
			{
			if(block_below_is_valid)
			{
				MapNode n = getNodeParent(v3s16(x, -1, z));
				if(n.light_propagates())
				{
					if(n.getLight(LIGHTBANK_DAY) == LIGHT_SUN
							&& sunlight_should_go_down == false)
						block_below_is_valid = false;
					else if(n.getLight(LIGHTBANK_DAY) != LIGHT_SUN
							&& sunlight_should_go_down == true)
						block_below_is_valid = false;
				}
			}//if
			}//try
			catch(InvalidPositionException &e)
			{
				/*std::cout<<"InvalidBlockException for bottom block node"
						<<std::endl;*/
				// Just no block below, no need to panic.
			}
		}
	}

	return block_below_is_valid;
}

void MapBlock::copyTo(VoxelManipulator &dst)
{
	v3s16 data_size(MAP_BLOCKSIZE, MAP_BLOCKSIZE, MAP_BLOCKSIZE);
	VoxelArea data_area(v3s16(0,0,0), data_size - v3s16(1,1,1));
	
	dst.copyFrom(data, data_area, v3s16(0,0,0),
			getPosRelative(), data_size);
}

/*void getPseudoObjects(v3f origin, f32 max_d,
		core::array<DistanceSortedObject> &dest)
{
}*/
void MapBlock::stepObjects(float dtime, bool server, u32 daynight_ratio)
{
	/*
		Step objects
	*/
	m_objects.step(dtime, server, daynight_ratio);
	
	/*
		Spawn some objects at random.

		Use dayNightDiffed() to approximate being near ground level
	*/
	if(m_spawn_timer < -999)
	{
		m_spawn_timer = 60;
	}
	if(dayNightDiffed() == true && getObjectCount() == 0)
	{
		m_spawn_timer -= dtime;
		if(m_spawn_timer <= 0.0)
		{
			m_spawn_timer += myrand() % 300;
			
			v2s16 p2d(
				(myrand()%(MAP_BLOCKSIZE-1))+0,
				(myrand()%(MAP_BLOCKSIZE-1))+0
			);

			s16 y = getGroundLevel(p2d);
			
			if(y >= 0)
			{
				v3s16 p(p2d.X, y+1, p2d.Y);

				if(getNode(p).d == CONTENT_AIR
						&& getNode(p).getLightBlend(daynight_ratio) <= 11)
				{
					RatObject *obj = new RatObject(NULL, -1, intToFloat(p));
					addObject(obj);
				}
			}
		}
	}

	setChangedFlag();
}


void MapBlock::updateDayNightDiff()
{
	if(data == NULL)
	{
		m_day_night_differs = false;
		return;
	}

	bool differs = false;

	/*
		Check if any lighting value differs
	*/
	for(u32 i=0; i<MAP_BLOCKSIZE*MAP_BLOCKSIZE*MAP_BLOCKSIZE; i++)
	{
		MapNode &n = data[i];
		if(n.getLight(LIGHTBANK_DAY) != n.getLight(LIGHTBANK_NIGHT))
		{
			differs = true;
			break;
		}
	}

	/*
		If some lighting values differ, check if the whole thing is
		just air. If it is, differ = false
	*/
	if(differs)
	{
		bool only_air = true;
		for(u32 i=0; i<MAP_BLOCKSIZE*MAP_BLOCKSIZE*MAP_BLOCKSIZE; i++)
		{
			MapNode &n = data[i];
			if(n.d != CONTENT_AIR)
			{
				only_air = false;
				break;
			}
		}
		if(only_air)
			differs = false;
	}

	// Set member variable
	m_day_night_differs = differs;
}

s16 MapBlock::getGroundLevel(v2s16 p2d)
{
	if(isDummy())
		return -3;
	try
	{
		s16 y = MAP_BLOCKSIZE-1;
		for(; y>=0; y--)
		{
			//if(is_ground_content(getNodeRef(p2d.X, y, p2d.Y).d))
			if(content_features(getNodeRef(p2d.X, y, p2d.Y).d).walkable)
			{
				if(y == MAP_BLOCKSIZE-1)
					return -2;
				else
					return y;
			}
		}
		return -1;
	}
	catch(InvalidPositionException &e)
	{
		return -3;
	}
}

/*
	Serialization
*/

void MapBlock::serialize(std::ostream &os, u8 version)
{
	if(!ser_ver_supported(version))
		throw VersionMismatchException("ERROR: MapBlock format not supported");
	
	if(data == NULL)
	{
		throw SerializationError("ERROR: Not writing dummy block.");
	}
	
	// These have no compression
	if(version <= 3 || version == 5 || version == 6)
	{
		u32 nodecount = MAP_BLOCKSIZE*MAP_BLOCKSIZE*MAP_BLOCKSIZE;
		
		u32 buflen = 1 + nodecount * MapNode::serializedLength(version);
		SharedBuffer<u8> dest(buflen);

		dest[0] = is_underground;
		for(u32 i=0; i<nodecount; i++)
		{
			u32 s = 1 + i * MapNode::serializedLength(version);
			data[i].serialize(&dest[s], version);
		}
		
		os.write((char*)*dest, dest.getSize());
	}
	else if(version <= 10)
	{
		/*
			With compression.
			Compress the materials and the params separately.
		*/
		
		// First byte
		os.write((char*)&is_underground, 1);

		u32 nodecount = MAP_BLOCKSIZE*MAP_BLOCKSIZE*MAP_BLOCKSIZE;

		// Get and compress materials
		SharedBuffer<u8> materialdata(nodecount);
		for(u32 i=0; i<nodecount; i++)
		{
			materialdata[i] = data[i].d;
		}
		compress(materialdata, os, version);

		// Get and compress lights
		SharedBuffer<u8> lightdata(nodecount);
		for(u32 i=0; i<nodecount; i++)
		{
			lightdata[i] = data[i].param;
		}
		compress(lightdata, os, version);
		
		if(version >= 10)
		{
			// Get and compress param2
			SharedBuffer<u8> param2data(nodecount);
			for(u32 i=0; i<nodecount; i++)
			{
				param2data[i] = data[i].param2;
			}
			compress(param2data, os, version);
		}
	}
	// All other versions (newest)
	else
	{
		// First byte
		u8 flags = 0;
		if(is_underground)
			flags |= 1;
		if(m_day_night_differs)
			flags |= 2;
		os.write((char*)&flags, 1);

		u32 nodecount = MAP_BLOCKSIZE*MAP_BLOCKSIZE*MAP_BLOCKSIZE;

		/*
			Get data
		*/

		SharedBuffer<u8> databuf(nodecount*3);

		// Get contents
		for(u32 i=0; i<nodecount; i++)
		{
			databuf[i] = data[i].d;
		}

		// Get params
		for(u32 i=0; i<nodecount; i++)
		{
			databuf[i+nodecount] = data[i].param;
		}

		// Get param2
		for(u32 i=0; i<nodecount; i++)
		{
			databuf[i+nodecount*2] = data[i].param2;
		}

		/*
			Compress data to output stream
		*/

		compress(databuf, os, version);
	}
}

void MapBlock::deSerialize(std::istream &is, u8 version)
{
	if(!ser_ver_supported(version))
		throw VersionMismatchException("ERROR: MapBlock format not supported");

	// These have no compression
	if(version <= 3 || version == 5 || version == 6)
	{
		u32 nodecount = MAP_BLOCKSIZE*MAP_BLOCKSIZE*MAP_BLOCKSIZE;
		char tmp;
		is.read(&tmp, 1);
		if(is.gcount() != 1)
			throw SerializationError
					("MapBlock::deSerialize: no enough input data");
		is_underground = tmp;
		for(u32 i=0; i<nodecount; i++)
		{
			s32 len = MapNode::serializedLength(version);
			SharedBuffer<u8> d(len);
			is.read((char*)*d, len);
			if(is.gcount() != len)
				throw SerializationError
						("MapBlock::deSerialize: no enough input data");
			data[i].deSerialize(*d, version);
		}
	}
	else if(version <= 10)
	{
		u32 nodecount = MAP_BLOCKSIZE*MAP_BLOCKSIZE*MAP_BLOCKSIZE;

		u8 t8;
		is.read((char*)&t8, 1);
		is_underground = t8;

		{
			// Uncompress and set material data
			std::ostringstream os(std::ios_base::binary);
			decompress(is, os, version);
			std::string s = os.str();
			if(s.size() != nodecount)
				throw SerializationError
						("MapBlock::deSerialize: invalid format");
			for(u32 i=0; i<s.size(); i++)
			{
				data[i].d = s[i];
			}
		}
		{
			// Uncompress and set param data
			std::ostringstream os(std::ios_base::binary);
			decompress(is, os, version);
			std::string s = os.str();
			if(s.size() != nodecount)
				throw SerializationError
						("MapBlock::deSerialize: invalid format");
			for(u32 i=0; i<s.size(); i++)
			{
				data[i].param = s[i];
			}
		}
	
		if(version >= 10)
		{
			// Uncompress and set param2 data
			std::ostringstream os(std::ios_base::binary);
			decompress(is, os, version);
			std::string s = os.str();
			if(s.size() != nodecount)
				throw SerializationError
						("MapBlock::deSerialize: invalid format");
			for(u32 i=0; i<s.size(); i++)
			{
				data[i].param2 = s[i];
			}
		}
	}
	// All other versions (newest)
	else
	{
		u32 nodecount = MAP_BLOCKSIZE*MAP_BLOCKSIZE*MAP_BLOCKSIZE;

		u8 flags;
		is.read((char*)&flags, 1);
		is_underground = (flags & 1) ? true : false;
		m_day_night_differs = (flags & 2) ? true : false;

		// Uncompress data
		std::ostringstream os(std::ios_base::binary);
		decompress(is, os, version);
		std::string s = os.str();
		if(s.size() != nodecount*3)
			throw SerializationError
					("MapBlock::deSerialize: invalid format");

		// Set contents
		for(u32 i=0; i<nodecount; i++)
		{
			data[i].d = s[i];
		}
		// Set params
		for(u32 i=0; i<nodecount; i++)
		{
			data[i].param = s[i+nodecount];
		}
		// Set param2
		for(u32 i=0; i<nodecount; i++)
		{
			data[i].param2 = s[i+nodecount*2];
		}
	}
}


//END
