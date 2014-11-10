/******************************************************************************
 * The MIT License (MIT)
 * 
 * Copyright (c) 2014 Crytek
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 ******************************************************************************/


#include "driver/gl/gl_manager.h"
#include "driver/gl/gl_driver.h"

struct VertexArrayInitialData
{
	VertexArrayInitialData()
	{
		RDCEraseEl(*this);
	}
	uint32_t enabled;
	uint32_t vbslot;
	uint32_t offset;
	GLenum   type;
	int32_t  normalized;
	uint32_t integer;
	uint32_t size;
};

bool GLResourceManager::SerialisableResource(ResourceId id, GLResourceRecord *record)
{
	if(id == m_GL->GetContextResourceID())
		return false;
	return true;
}

bool GLResourceManager::Need_InitialStateChunk(GLResource res)
{
	return res.Namespace != eResBuffer;
}

bool GLResourceManager::Prepare_InitialState(GLResource res)
{
	ResourceId Id = GetID(res);
	
	const GLHookSet &gl = m_GL->m_Real;

	if(res.Namespace == eResBuffer)
	{
		GLResourceRecord *record = GetResourceRecord(res);

		// TODO copy this to an immutable buffer elsewhere and SetInitialContents() it.
		// then only do the readback in Serialise_InitialState
		
		GLint length;
		gl.glGetNamedBufferParameterivEXT(res.name, eGL_BUFFER_SIZE, &length);
	
		gl.glGetNamedBufferSubDataEXT(res.name, 0, length, record->GetDataPtr());
	}
	else if(res.Namespace == eResTexture)
	{
		WrappedOpenGL::TextureData &details = m_GL->m_Textures[Id];
		GLenum binding = TextureBinding(details.curType);

		GLuint oldtex = 0;
		gl.glGetIntegerv(binding, (GLint *)&oldtex);
		
		GLuint tex;
		gl.glGenTextures(1, &tex);

		gl.glBindTexture(details.curType, res.name);

		int depth = details.depth;
		if(details.curType != eGL_TEXTURE_3D) depth = 1;

		int mips = 0;
		GLint isComp = 0;

		GLenum queryType = details.curType;
		if(queryType == eGL_TEXTURE_CUBE_MAP || queryType == eGL_TEXTURE_CUBE_MAP_ARRAY)
			queryType = eGL_TEXTURE_CUBE_MAP_POSITIVE_X;
		gl.glGetTexLevelParameteriv(queryType, 0, eGL_TEXTURE_COMPRESSED, &isComp);

		GLint immut = 0;
		gl.glGetTexParameteriv(details.curType, eGL_TEXTURE_IMMUTABLE_FORMAT, &immut);

		if(immut)
			gl.glGetTexParameteriv(details.curType, eGL_TEXTURE_IMMUTABLE_LEVELS, (GLint *)&mips);
		else
			gl.glGetTexParameteriv(details.curType, eGL_TEXTURE_MAX_LEVEL, (GLint *)&mips);

		gl.glBindTexture(details.curType, tex);

		// create texture of identical format/size to store initial contents
		if(details.curType == eGL_TEXTURE_2D_MULTISAMPLE)
		{
			gl.glTextureStorage2DMultisampleEXT(tex, details.curType, details.depth, details.internalFormat, details.width, details.height, GL_TRUE);
			mips = 1;
		}
		else if(details.dimension == 1)
		{
			gl.glTextureStorage1DEXT(tex, details.curType, mips, details.internalFormat, details.width);
		}
		else if(details.dimension == 2)
		{
			gl.glTextureStorage2DEXT(tex, details.curType, mips, details.internalFormat, details.width, details.height);
		}
		else if(details.dimension == 3)
		{
			gl.glTextureStorage3DEXT(tex, details.curType, mips, details.internalFormat, details.width, details.height, details.depth);
		}

		// copy over mips
		for(int i=0; i < mips; i++)
		{
			int w = RDCMAX(details.width>>i, 1);
			int h = RDCMAX(details.height>>i, 1);
			int d = RDCMAX(details.depth>>i, 1);

			if(details.curType == eGL_TEXTURE_CUBE_MAP)
				d *= 6;

			gl.glCopyImageSubData(res.name, details.curType, i, 0, 0, 0, tex, details.curType, i, 0, 0, 0, w, h, d);
		}
		
		SetInitialContents(Id, InitialContentData(TextureRes(res.Context, tex), 0, NULL));
		
		gl.glBindTexture(details.curType, oldtex);
	}
	else if(res.Namespace == eResVertexArray)
	{
		GLuint VAO = 0;
		gl.glGetIntegerv(eGL_VERTEX_ARRAY_BINDING, (GLint *)&VAO);

		gl.glBindVertexArray(res.name);

		VertexArrayInitialData *data = (VertexArrayInitialData *)new byte[sizeof(VertexArrayInitialData)*16];

		for(GLuint i=0; i < 16; i++)
		{
			gl.glGetVertexAttribiv(i, eGL_VERTEX_ATTRIB_ARRAY_ENABLED, (GLint *)&data[i].enabled);
			gl.glGetVertexAttribiv(i, eGL_VERTEX_ATTRIB_BINDING, (GLint *)&data[i].vbslot);
			gl.glGetVertexAttribiv(i, eGL_VERTEX_ATTRIB_RELATIVE_OFFSET, (GLint*)&data[i].offset);
			gl.glGetVertexAttribiv(i, eGL_VERTEX_ATTRIB_ARRAY_TYPE, (GLint *)&data[i].type);
			gl.glGetVertexAttribiv(i, eGL_VERTEX_ATTRIB_ARRAY_NORMALIZED, (GLint *)&data[i].normalized);
			gl.glGetVertexAttribiv(i, eGL_VERTEX_ATTRIB_ARRAY_INTEGER, (GLint *)&data[i].integer);
			gl.glGetVertexAttribiv(i, eGL_VERTEX_ATTRIB_ARRAY_SIZE, (GLint *)&data[i].size);
		}

		SetInitialContents(Id, InitialContentData(GLResource(MakeNullResource), 0, (byte *)data));

		gl.glBindVertexArray(VAO);
	}
	else
	{
		RDCERR("Unexpected type of resource requiring initial state");
	}

	return true;
}

bool GLResourceManager::Force_InitialState(GLResource res)
{
	return false;
}

bool GLResourceManager::Serialise_InitialState(GLResource res)
{
	ResourceId Id = ResourceId();

	if(m_State >= WRITING)
	{
		Id = GetID(res);

		if(res.Namespace != eResBuffer)
			m_pSerialiser->Serialise("Id", Id);
	}
	else
	{
		m_pSerialiser->Serialise("Id", Id);
	}
	
	if(m_State < WRITING)
	{
		if(HasLiveResource(Id))
			res = GetLiveResource(Id);
		else
			res = GLResource(MakeNullResource);
	}
	
	const GLHookSet &gl = m_GL->m_Real;
	
	if(res.Namespace == eResBuffer)
	{
		// Nothing to serialize
	}
	else if(res.Namespace == eResTexture)
	{
		if(m_State >= WRITING)
		{
			WrappedOpenGL::TextureData &details = m_GL->m_Textures[Id];

			GLuint tex = GetInitialContents(Id).resource.name;

			GLuint ppb = 0;
			gl.glGetIntegerv(eGL_PIXEL_PACK_BUFFER_BINDING, (GLint *)&ppb);
			gl.glBindBuffer(eGL_PIXEL_PACK_BUFFER, 0);

			GLint packParams[8];
			gl.glGetIntegerv(eGL_PACK_SWAP_BYTES, &packParams[0]);
			gl.glGetIntegerv(eGL_PACK_LSB_FIRST, &packParams[1]);
			gl.glGetIntegerv(eGL_PACK_ROW_LENGTH, &packParams[2]);
			gl.glGetIntegerv(eGL_PACK_IMAGE_HEIGHT, &packParams[3]);
			gl.glGetIntegerv(eGL_PACK_SKIP_PIXELS, &packParams[4]);
			gl.glGetIntegerv(eGL_PACK_SKIP_ROWS, &packParams[5]);
			gl.glGetIntegerv(eGL_PACK_SKIP_IMAGES, &packParams[6]);
			gl.glGetIntegerv(eGL_PACK_ALIGNMENT, &packParams[7]);

			gl.glPixelStorei(eGL_PACK_SWAP_BYTES, 0);
			gl.glPixelStorei(eGL_PACK_LSB_FIRST, 0);
			gl.glPixelStorei(eGL_PACK_ROW_LENGTH, 0);
			gl.glPixelStorei(eGL_PACK_IMAGE_HEIGHT, 0);
			gl.glPixelStorei(eGL_PACK_SKIP_PIXELS, 0);
			gl.glPixelStorei(eGL_PACK_SKIP_ROWS, 0);
			gl.glPixelStorei(eGL_PACK_SKIP_IMAGES, 0);
			gl.glPixelStorei(eGL_PACK_ALIGNMENT, 1);
			
			GLuint prevtex = 0;
			gl.glGetIntegerv(TextureBinding(details.curType), (GLint *)&prevtex);

			gl.glBindTexture(details.curType, res.name);
			
			int imgmips = 0;
			GLint isComp = 0;
			
			GLenum queryType = details.curType;
			if(queryType == eGL_TEXTURE_CUBE_MAP || queryType == eGL_TEXTURE_CUBE_MAP_ARRAY)
				queryType = eGL_TEXTURE_CUBE_MAP_POSITIVE_X;

			gl.glGetTexLevelParameteriv(queryType, 0, eGL_TEXTURE_COMPRESSED, &isComp);

			GLint immut = 0;
			gl.glGetTexParameteriv(details.curType, eGL_TEXTURE_IMMUTABLE_FORMAT, &immut);

			if(immut)
				gl.glGetTexParameteriv(details.curType, eGL_TEXTURE_IMMUTABLE_LEVELS, (GLint *)&imgmips);
			else
				gl.glGetTexParameteriv(details.curType, eGL_TEXTURE_MAX_LEVEL, (GLint *)&imgmips);

			SERIALISE_ELEMENT(uint32_t, width, details.width);
			SERIALISE_ELEMENT(uint32_t, height, details.height);
			SERIALISE_ELEMENT(uint32_t, depth, details.depth);
			SERIALISE_ELEMENT(uint32_t, dim, details.dimension);
			SERIALISE_ELEMENT(GLenum, t, details.curType);
			SERIALISE_ELEMENT(GLenum, f, details.internalFormat);
			SERIALISE_ELEMENT(int, mips, imgmips);
			
			SERIALISE_ELEMENT(bool, isCompressed, isComp != 0);

			if(isCompressed)
			{
				for(int i=0; i < mips; i++)
				{
					int w = RDCMAX(details.width>>i, 1);
					int h = RDCMAX(details.height>>i, 1);
					int d = RDCMAX(details.depth>>i, 1);
					
					GLenum targets[] = {
						eGL_TEXTURE_CUBE_MAP_POSITIVE_X,
						eGL_TEXTURE_CUBE_MAP_NEGATIVE_X,
						eGL_TEXTURE_CUBE_MAP_POSITIVE_Y,
						eGL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
						eGL_TEXTURE_CUBE_MAP_POSITIVE_Z,
						eGL_TEXTURE_CUBE_MAP_NEGATIVE_Z,
					};

					int count = ARRAY_COUNT(targets);
						
					if(details.curType != eGL_TEXTURE_CUBE_MAP)
					{
						targets[0] = details.curType;
						count = 1;
					}

					for(int trg=0; trg < count; trg++)
					{
						GLint compSize;
						gl.glGetTextureLevelParameterivEXT(tex, targets[trg], i, eGL_TEXTURE_COMPRESSED_IMAGE_SIZE, &compSize);

						size_t size = compSize;

						byte *buf = new byte[size];

						gl.glGetCompressedTextureImageEXT(tex, targets[trg], i, buf);

						m_pSerialiser->SerialiseBuffer("image", buf, size);

						delete[] buf;
					}
				}
			}
			else
			{
				GLenum fmt = GetBaseFormat(details.internalFormat);
				GLenum type = GetDataType(details.internalFormat);
					
				size_t size = GetByteSize(details.width, details.height, details.depth, fmt, type, 0);

				byte *buf = new byte[size];

				for(int i=0; i < mips; i++)
				{
					int w = RDCMAX(details.width>>i, 1);
					int h = RDCMAX(details.height>>i, 1);
					int d = RDCMAX(details.depth>>i, 1);
					
					size = GetByteSize(w, h, d, fmt, type, 0);
					
					GLenum targets[] = {
						eGL_TEXTURE_CUBE_MAP_POSITIVE_X,
						eGL_TEXTURE_CUBE_MAP_NEGATIVE_X,
						eGL_TEXTURE_CUBE_MAP_POSITIVE_Y,
						eGL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
						eGL_TEXTURE_CUBE_MAP_POSITIVE_Z,
						eGL_TEXTURE_CUBE_MAP_NEGATIVE_Z,
					};

					int count = ARRAY_COUNT(targets);
						
					if(details.curType != eGL_TEXTURE_CUBE_MAP)
					{
						targets[0] = details.curType;
						count = 1;
					}

					for(int trg=0; trg < count; trg++)
					{
						gl.glGetTextureImageEXT(tex, targets[trg], i, fmt, type, buf);

						m_pSerialiser->SerialiseBuffer("image", buf, size);
					}
				}

				delete[] buf;
			}
			
			gl.glBindTexture(details.curType, prevtex);

			gl.glBindBuffer(eGL_PIXEL_PACK_BUFFER, ppb);

			gl.glPixelStorei(eGL_PACK_SWAP_BYTES, packParams[0]);
			gl.glPixelStorei(eGL_PACK_LSB_FIRST, packParams[1]);
			gl.glPixelStorei(eGL_PACK_ROW_LENGTH, packParams[2]);
			gl.glPixelStorei(eGL_PACK_IMAGE_HEIGHT, packParams[3]);
			gl.glPixelStorei(eGL_PACK_SKIP_PIXELS, packParams[4]);
			gl.glPixelStorei(eGL_PACK_SKIP_ROWS, packParams[5]);
			gl.glPixelStorei(eGL_PACK_SKIP_IMAGES, packParams[6]);
			gl.glPixelStorei(eGL_PACK_ALIGNMENT, packParams[7]);
		}
		else
		{
			GLuint tex = 0;
			gl.glGenTextures(1, &tex);

			GLuint pub = 0;
			gl.glGetIntegerv(eGL_PIXEL_UNPACK_BUFFER_BINDING, (GLint *)&pub);
			gl.glBindBuffer(eGL_PIXEL_UNPACK_BUFFER, 0);

			GLint unpackParams[8];
			gl.glGetIntegerv(eGL_UNPACK_SWAP_BYTES, &unpackParams[0]);
			gl.glGetIntegerv(eGL_UNPACK_LSB_FIRST, &unpackParams[1]);
			gl.glGetIntegerv(eGL_UNPACK_ROW_LENGTH, &unpackParams[2]);
			gl.glGetIntegerv(eGL_UNPACK_IMAGE_HEIGHT, &unpackParams[3]);
			gl.glGetIntegerv(eGL_UNPACK_SKIP_PIXELS, &unpackParams[4]);
			gl.glGetIntegerv(eGL_UNPACK_SKIP_ROWS, &unpackParams[5]);
			gl.glGetIntegerv(eGL_UNPACK_SKIP_IMAGES, &unpackParams[6]);
			gl.glGetIntegerv(eGL_UNPACK_ALIGNMENT, &unpackParams[7]);

			gl.glPixelStorei(eGL_UNPACK_SWAP_BYTES, 0);
			gl.glPixelStorei(eGL_UNPACK_LSB_FIRST, 0);
			gl.glPixelStorei(eGL_UNPACK_ROW_LENGTH, 0);
			gl.glPixelStorei(eGL_UNPACK_IMAGE_HEIGHT, 0);
			gl.glPixelStorei(eGL_UNPACK_SKIP_PIXELS, 0);
			gl.glPixelStorei(eGL_UNPACK_SKIP_ROWS, 0);
			gl.glPixelStorei(eGL_UNPACK_SKIP_IMAGES, 0);
			gl.glPixelStorei(eGL_UNPACK_ALIGNMENT, 1);

			SERIALISE_ELEMENT(uint32_t, width, 0);
			SERIALISE_ELEMENT(uint32_t, height, 0);
			SERIALISE_ELEMENT(uint32_t, depth, 0);
			SERIALISE_ELEMENT(uint32_t, dim, 0);
			SERIALISE_ELEMENT(GLenum, textype, eGL_NONE);
			SERIALISE_ELEMENT(GLenum, internalformat, eGL_NONE);
			SERIALISE_ELEMENT(int, mips, 0);
			SERIALISE_ELEMENT(bool, isCompressed, false);
			
			GLuint prevtex = 0;
			gl.glGetIntegerv(TextureBinding(textype), (GLint *)&prevtex);
			
			gl.glBindTexture(textype, tex);

			// create texture of identical format/size to store initial contents
			if(textype == eGL_TEXTURE_2D_MULTISAMPLE)
			{
				gl.glTextureStorage2DMultisampleEXT(tex, textype, depth, internalformat, width, height, GL_TRUE);
				mips = 1;
			}
			else if(dim == 1)
			{
				gl.glTextureStorage1DEXT(tex, textype, mips, internalformat, width);
			}
			else if(dim == 2)
			{
				gl.glTextureStorage2DEXT(tex, textype, mips, internalformat, width, height);
			}
			else if(dim == 3)
			{
				gl.glTextureStorage3DEXT(tex, textype, mips, internalformat, width, height, depth);
			}

			if(isCompressed)
			{
				for(int i=0; i < mips; i++)
				{
					uint32_t w = RDCMAX(width>>i, 1U);
					uint32_t h = RDCMAX(height>>i, 1U);
					uint32_t d = RDCMAX(depth>>i, 1U);
						
					GLenum targets[] = {
						eGL_TEXTURE_CUBE_MAP_POSITIVE_X,
						eGL_TEXTURE_CUBE_MAP_NEGATIVE_X,
						eGL_TEXTURE_CUBE_MAP_POSITIVE_Y,
						eGL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
						eGL_TEXTURE_CUBE_MAP_POSITIVE_Z,
						eGL_TEXTURE_CUBE_MAP_NEGATIVE_Z,
					};

					int count = ARRAY_COUNT(targets);
						
					if(textype != eGL_TEXTURE_CUBE_MAP)
					{
						targets[0] = textype;
						count = 1;
					}

					for(int trg=0; trg < count; trg++)
					{
						size_t size = 0;
						byte *buf = NULL;

						m_pSerialiser->SerialiseBuffer("image", buf, size);

						if(dim == 1)
							gl.glCompressedTextureSubImage1DEXT(tex, targets[trg], i, 0, w, internalformat, (GLsizei)size, buf);
						else if(dim == 2)
							gl.glCompressedTextureSubImage2DEXT(tex, targets[trg], i, 0, 0, w, h, internalformat, (GLsizei)size, buf);
						else if(dim == 3)
							gl.glCompressedTextureSubImage3DEXT(tex, targets[trg], i, 0, 0, 0, w, h, d, internalformat, (GLsizei)size, buf);

						delete[] buf;
					}
				}
			}
			else
			{
				GLenum fmt = GetBaseFormat(internalformat);
				GLenum type = GetDataType(internalformat);

				for(int i=0; i < mips; i++)
				{
					uint32_t w = RDCMAX(width>>i, 1U);
					uint32_t h = RDCMAX(height>>i, 1U);
					uint32_t d = RDCMAX(depth>>i, 1U);
					
					GLenum targets[] = {
						eGL_TEXTURE_CUBE_MAP_POSITIVE_X,
						eGL_TEXTURE_CUBE_MAP_NEGATIVE_X,
						eGL_TEXTURE_CUBE_MAP_POSITIVE_Y,
						eGL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
						eGL_TEXTURE_CUBE_MAP_POSITIVE_Z,
						eGL_TEXTURE_CUBE_MAP_NEGATIVE_Z,
					};

					int count = ARRAY_COUNT(targets);
						
					if(textype != eGL_TEXTURE_CUBE_MAP)
					{
						targets[0] = textype;
						count = 1;
					}
					
					for(int trg=0; trg < count; trg++)
					{
						size_t size = 0;
						byte *buf = NULL;
						m_pSerialiser->SerialiseBuffer("image", buf, size);

						if(dim == 1)
							gl.glTextureSubImage1DEXT(tex, targets[trg], i, 0, w, fmt, type, buf);
						else if(dim == 2)
							gl.glTextureSubImage2DEXT(tex, targets[trg], i, 0, 0, w, h, fmt, type, buf);
						else if(dim == 3)
							gl.glTextureSubImage3DEXT(tex, targets[trg], i, 0, 0, 0, w, h, d, fmt, type, buf);

						delete[] buf;
					}
				}
			}
			
			SetInitialContents(Id, InitialContentData(TextureRes(m_GL->GetCtx(), tex), 0, NULL));

			gl.glBindBuffer(eGL_PIXEL_UNPACK_BUFFER, pub);
			
			gl.glBindTexture(textype, prevtex);

			gl.glPixelStorei(eGL_UNPACK_SWAP_BYTES, unpackParams[0]);
			gl.glPixelStorei(eGL_UNPACK_LSB_FIRST, unpackParams[1]);
			gl.glPixelStorei(eGL_UNPACK_ROW_LENGTH, unpackParams[2]);
			gl.glPixelStorei(eGL_UNPACK_IMAGE_HEIGHT, unpackParams[3]);
			gl.glPixelStorei(eGL_UNPACK_SKIP_PIXELS, unpackParams[4]);
			gl.glPixelStorei(eGL_UNPACK_SKIP_ROWS, unpackParams[5]);
			gl.glPixelStorei(eGL_UNPACK_SKIP_IMAGES, unpackParams[6]);
			gl.glPixelStorei(eGL_UNPACK_ALIGNMENT, unpackParams[7]);
		}
	}
	else if(res.Namespace == eResVertexArray)
	{
		VertexArrayInitialData data[16];

		if(m_State >= WRITING)
		{
			VertexArrayInitialData *initialdata = (VertexArrayInitialData *)GetInitialContents(Id).blob;
			memcpy(data, initialdata, sizeof(data));
		}

		for(GLuint i=0; i < 16; i++)
		{
			m_pSerialiser->Serialise("data[].enabled", data[i].enabled);
			m_pSerialiser->Serialise("data[].vbslot", data[i].vbslot);
			m_pSerialiser->Serialise("data[].offset", data[i].offset);
			m_pSerialiser->Serialise("data[].type", data[i].type);
			m_pSerialiser->Serialise("data[].normalized", data[i].normalized);
			m_pSerialiser->Serialise("data[].integer", data[i].integer);
			m_pSerialiser->Serialise("data[].size", data[i].size);
		}

		if(m_State < WRITING)
		{
			byte *blob = new byte[sizeof(data)];
			memcpy(blob, data, sizeof(data));

			SetInitialContents(Id, InitialContentData(GLResource(MakeNullResource), 0, blob));
		}
	}
	else
	{
		RDCERR("Unexpected type of resource requiring initial state");
	}

	return true;
}

void GLResourceManager::Create_InitialState(ResourceId id, GLResource live, bool hasData)
{
	if(live.Namespace == eResTexture)
	{
		GLNOTIMP("Need to set initial clear state for textures without an initial state");
	}
	else if(live.Namespace != eResBuffer)
	{
		RDCUNIMPLEMENTED("Expect all initial states to be created & not skipped, presently");
	}
}

void GLResourceManager::Apply_InitialState(GLResource live, InitialContentData initial)
{
	const GLHookSet &gl = m_GL->m_Real;
	
	if(live.Namespace == eResTexture)
	{
		ResourceId Id = GetID(live);
		WrappedOpenGL::TextureData &details = m_GL->m_Textures[Id];

		GLuint tex = initial.resource.name;

		gl.glBindTexture(details.curType, tex);
		
		int mips = 0;

		GLint immut = 0;
		gl.glGetTexParameteriv(details.curType, eGL_TEXTURE_IMMUTABLE_FORMAT, &immut);

		if(immut)
			gl.glGetTexParameteriv(details.curType, eGL_TEXTURE_IMMUTABLE_LEVELS, (GLint *)&mips);
		else
			gl.glGetTexParameteriv(details.curType, eGL_TEXTURE_MAX_LEVEL, (GLint *)&mips);

		// copy over mips
		for(int i=0; i < mips; i++)
		{
			int w = RDCMAX(details.width>>i, 1);
			int h = RDCMAX(details.height>>i, 1);
			int d = RDCMAX(details.depth>>i, 1);
			
			if(details.curType == eGL_TEXTURE_CUBE_MAP)
				d *= 6;

			gl.glCopyImageSubData(tex, details.curType, i, 0, 0, 0, live.name, details.curType, i, 0, 0, 0, w, h, d);
		}
	}
	else if(live.Namespace == eResVertexArray)
	{
		GLuint VAO = 0;
		gl.glGetIntegerv(eGL_VERTEX_ARRAY_BINDING, (GLint *)&VAO);

		gl.glBindVertexArray(live.name);
	
		VertexArrayInitialData *initialdata = (VertexArrayInitialData *)initial.blob;	

		for(GLuint i=0; i < 16; i++)
		{
			if(initialdata[i].enabled)
				gl.glEnableVertexAttribArray(i);
			else
				gl.glDisableVertexAttribArray(i);

			gl.glVertexAttribBinding(i, initialdata[i].vbslot);

			if(initialdata[i].integer == 0)
				gl.glVertexAttribFormat(i, initialdata[i].size, initialdata[i].type, (GLboolean)initialdata[i].normalized, initialdata[i].offset);
			else
				gl.glVertexAttribIFormat(i, initialdata[i].size, initialdata[i].type, initialdata[i].offset);
		}

		gl.glBindVertexArray(VAO);
	}
	else
	{
		RDCERR("Unexpected type of resource requiring initial state");
	}
}
