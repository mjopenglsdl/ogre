/*
-----------------------------------------------------------------------------
This source file is part of OGRE
    (Object-oriented Graphics Rendering Engine)
For the latest info, see http://www.ogre3d.org/

Copyright (c) 2000-2013 Torus Knot Software Ltd

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
-----------------------------------------------------------------------------
*/

#include "OgreGL3PlusHardwareIndexBuffer.h"
#include "OgreGL3PlusHardwareBufferManager.h"
#include "OgreGL3PlusRenderSystem.h"
#include "OgreRoot.h"

namespace Ogre {
    GL3PlusHardwareIndexBuffer::GL3PlusHardwareIndexBuffer(HardwareBufferManagerBase* mgr, 
													 IndexType idxType,
                                                     size_t numIndexes,
                                                     HardwareBuffer::Usage usage,
                                                     bool useShadowBuffer)
    : HardwareIndexBuffer(mgr, idxType, numIndexes, usage, false, false)//useShadowBuffer)
    {
        glGenBuffers(1, &mBufferId);
        GL_CHECK_ERROR

        if (!mBufferId)
        {
            OGRE_EXCEPT(Exception::ERR_INTERNAL_ERROR,
                "Cannot create GL index buffer",
                "GL3PlusHardwareIndexBuffer::GL3PlusHardwareIndexBuffer");
        }

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mBufferId);
        GL_CHECK_ERROR

        glBufferData(GL_ELEMENT_ARRAY_BUFFER, mSizeInBytes, NULL,
                     GL3PlusHardwareBufferManager::getGLUsage(usage));
        GL_CHECK_ERROR
//        std::cerr << "creating index buffer " << mBufferId << std::endl;
    }

    GL3PlusHardwareIndexBuffer::~GL3PlusHardwareIndexBuffer()
    {
        glDeleteBuffers(1, &mBufferId);
        GL_CHECK_ERROR
    }

    void* GL3PlusHardwareIndexBuffer::lockImpl(size_t offset,
                                            size_t length,
                                            LockOptions options)
    {
        if(mIsLocked)
        {
            OGRE_EXCEPT(Exception::ERR_INTERNAL_ERROR,
                        "Invalid attempt to lock an index buffer that has already been locked",
                        "GL3PlusHardwareIndexBuffer::lock");
        }

        void* retPtr = 0;
        GLenum access = 0;
//		GL3PlusHardwareBufferManager* glBufManager = static_cast<GL3PlusHardwareBufferManager*>(HardwareBufferManager::getSingletonPtr());
//
//        // Try to use scratch buffers for smaller buffers
//        if(length < glBufManager->getGLMapBufferThreshold())
//        {
//            retPtr = glBufManager->allocateScratch((uint32)length);
//            if (retPtr)
//            {
//                mLockedToScratch = true;
//                mScratchOffset = offset;
//                mScratchSize = length;
//                mScratchPtr = retPtr;
//                mScratchUploadOnUnlock = (options != HBL_READ_ONLY);
//
//                if (options != HBL_DISCARD)
//                {
//					// have to read back the data before returning the pointer
//                    readData(offset, length, retPtr);
//                }
//            }
//        }

		if (!retPtr)
		{
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mBufferId);
            GL_CHECK_ERROR

			// Use glMapBuffer
			if (mUsage & HBU_WRITE_ONLY)
            {
				access |= GL_MAP_WRITE_BIT;
                access |= GL_MAP_FLUSH_EXPLICIT_BIT;
                if(options == HBL_DISCARD)
                {
                    // Discard the buffer
                    access |= GL_MAP_INVALIDATE_RANGE_BIT;
                }
            }
			else if (options == HBL_READ_ONLY)
				access |= GL_MAP_READ_BIT;
			else
				access |= GL_MAP_READ_BIT | GL_MAP_WRITE_BIT;

            // We explicitly flush when the buffer is unlocked
//            access |= GL_MAP_UNSYNCHRONIZED_BIT;

            void* pBuffer = glMapBufferRange(GL_ELEMENT_ARRAY_BUFFER, offset, length, access);
            GL_CHECK_ERROR

			if(pBuffer == 0)
			{
				OGRE_EXCEPT(Exception::ERR_INTERNAL_ERROR, 
					"Index Buffer: Out of memory", 
					"GL3PlusHardwareIndexBuffer::lock");
			}

			// return offsetted
			retPtr = static_cast<void*>(static_cast<unsigned char*>(pBuffer) + offset);

			mLockedToScratch = false;
		}
		mIsLocked = true;
        return retPtr;
    }

    void GL3PlusHardwareIndexBuffer::unlockImpl(void)
    {
        if (mLockedToScratch)
        {
            if (mScratchUploadOnUnlock)
            {
                    // have to write the data back to vertex buffer
                    writeData(mScratchOffset, mScratchSize, mScratchPtr,
                              mScratchOffset == 0 && mScratchSize == getSizeInBytes());
            }

            static_cast<GL3PlusHardwareBufferManager*>(
                    HardwareBufferManager::getSingletonPtr())->deallocateScratch(mScratchPtr);

            mLockedToScratch = false;
        }
        else
        {
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mBufferId);
            GL_CHECK_ERROR

			if (mUsage & HBU_WRITE_ONLY)
            {
                glFlushMappedBufferRange(GL_ELEMENT_ARRAY_BUFFER, mLockStart, mLockSize);
                GL_CHECK_ERROR
            }

			if(!glUnmapBuffer(GL_ELEMENT_ARRAY_BUFFER))
			{
                GL_CHECK_ERROR
				OGRE_EXCEPT(Exception::ERR_INTERNAL_ERROR, 
					"Buffer data corrupted, please reload", 
					"GL3PlusHardwareIndexBuffer::unlock");
			}
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
            GL_CHECK_ERROR
        }
        mIsLocked = false;
    }

    void GL3PlusHardwareIndexBuffer::readData(size_t offset,
                                           size_t length,
                                           void* pDest)
    {
        if(mUseShadowBuffer)
        {
            // get data from the shadow buffer
            void* srcData = mShadowBuffer->lock(offset, length, HBL_READ_ONLY);
            memcpy(pDest, srcData, length);
            mShadowBuffer->unlock();
        }
        else
        {
            glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, mBufferId );
            GL_CHECK_ERROR
            glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, offset, length, pDest);
            GL_CHECK_ERROR
        }
    }

    void GL3PlusHardwareIndexBuffer::writeData(size_t offset, size_t length,
                                            const void* pSource,
                                            bool discardWholeBuffer)
    {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mBufferId);
        GL_CHECK_ERROR

        // Update the shadow buffer
        if (mUseShadowBuffer)
        {
            void* destData = mShadowBuffer->lock(offset, length,
                                                  discardWholeBuffer ? HBL_DISCARD : HBL_NORMAL);
            memcpy(destData, pSource, length);
            mShadowBuffer->unlock();
        }

        if (offset == 0 && length == mSizeInBytes)
        {
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, mSizeInBytes, pSource,
                         GL3PlusHardwareBufferManager::getGLUsage(mUsage));
            GL_CHECK_ERROR
        }
        else
        {
            if (discardWholeBuffer)
            {
                glBufferData(GL_ELEMENT_ARRAY_BUFFER, mSizeInBytes, NULL,
                                GL3PlusHardwareBufferManager::getGLUsage(mUsage));
                GL_CHECK_ERROR
            }

            // Now update the real buffer
            glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, offset, length, pSource);
            GL_CHECK_ERROR
        }
    }

    void GL3PlusHardwareIndexBuffer::copyData(HardwareBuffer& srcBuffer, size_t srcOffset, 
                                               size_t dstOffset, size_t length, bool discardWholeBuffer)
    {
        // If the buffer is not in system memory we can use ARB_copy_buffers to do an optimised copy.
        if (srcBuffer.isSystemMemory())
        {
			HardwareBuffer::copyData(srcBuffer, srcOffset, dstOffset, length, discardWholeBuffer);
        }
        else
        {
            // Unbind the current buffer
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
            GL_CHECK_ERROR
            
            // Zero out this(destination) buffer
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mBufferId);
            GL_CHECK_ERROR
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, length, 0, GL3PlusHardwareBufferManager::getGLUsage(mUsage));
            GL_CHECK_ERROR
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
            GL_CHECK_ERROR
            
            // Do it the fast way.
            glBindBuffer(GL_COPY_READ_BUFFER, static_cast<GL3PlusHardwareIndexBuffer &>(srcBuffer).getGLBufferId());
            GL_CHECK_ERROR
            glBindBuffer(GL_COPY_WRITE_BUFFER, mBufferId);
            GL_CHECK_ERROR
            
            glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, srcOffset, dstOffset, length);
            GL_CHECK_ERROR
            
            glBindBuffer(GL_COPY_READ_BUFFER, 0);
            GL_CHECK_ERROR
            glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
            GL_CHECK_ERROR
        }
    }

    void GL3PlusHardwareIndexBuffer::_updateFromShadow(void)
    {
        if (mUseShadowBuffer && mShadowUpdated && !mSuppressHardwareUpdate)
        {
            const void *srcData = mShadowBuffer->lock(mLockStart, mLockSize,
                                                       HBL_READ_ONLY);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mBufferId);
            GL_CHECK_ERROR

            // Update whole buffer if possible, otherwise normal
            if (mLockStart == 0 && mLockSize == mSizeInBytes)
            {
                glBufferData(GL_ELEMENT_ARRAY_BUFFER, mSizeInBytes, srcData,
                             GL3PlusHardwareBufferManager::getGLUsage(mUsage));
                GL_CHECK_ERROR
            }
            else
            {
                glBufferSubData(GL_ELEMENT_ARRAY_BUFFER,
                                mLockStart, mLockSize, srcData);
                GL_CHECK_ERROR
            }

            mShadowBuffer->unlock();
            mShadowUpdated = false;
        }
    }
}
