
#include "OpenVRCompositorListener.h"
#include "OgreTextureGpu.h"
#include "OgreRenderSystem.h"
#include "Compositor/Pass/OgreCompositorPass.h"
#include "Compositor/Pass/OgreCompositorPassDef.h"
#include "Compositor/Pass/PassScene/OgreCompositorPassScene.h"

#include "Compositor/OgreCompositorWorkspace.h"

#include <iostream>

namespace Demo
{
    OpenVRCompositorListener::OpenVRCompositorListener(
            vr::IVRSystem *hmd, vr::IVRCompositor *vrCompositor,
            Ogre::TextureGpu *vrTexture, Ogre::Root *root,
            Ogre::CompositorWorkspace *workspace,
            Ogre::Camera *camera, Ogre::Camera *cullCamera ) :
        mHMD( hmd ),
        mVrCompositor( vrCompositor ),
        mVrTexture( vrTexture ),
        mRoot( root ),
        mRenderSystem( root->getRenderSystem() ),
        mWorkspace( workspace ),
        mValidPoseCount( 0 ),
        mApiTextureType( vr::TextureType_Invalid ),
        mCamera( camera ),
        mVrCullCamera( cullCamera ),
        mCullCameraOffset( Ogre::Vector3::ZERO ),
        mWaitingMode( VrWaitingMode::BeforeSceneGraph ),
        mFirstGlitchFreeMode( VrWaitingMode::NumVrWaitingModes ),
        mLastCamNear( 0 ),
        mLastCamFar( 0 ),
        mMustSyncAtEndOfFrame( false )
    {
        memset( mTrackedDevicePose, 0, sizeof( mTrackedDevicePose ) );
        memset( mDevicePose, 0, sizeof( mDevicePose ) );
        memset( &mVrData, 0, sizeof( mVrData ) );

        mCamera->setVrData( &mVrData );
        syncCameraProjection( true );

        mRoot->addFrameListener( this );
        mWorkspace->addListener( this );

        const Ogre::String &renderSystemName = mRenderSystem->getName();
        if( renderSystemName == "OpenGL 3+ Rendering Subsystem" )
            mApiTextureType = vr::TextureType_OpenGL;
        else if( renderSystemName == "Direct3D11 Rendering Subsystem" )
            mApiTextureType = vr::TextureType_DirectX;
        else if( renderSystemName == "Metal Rendering Subsystem" )
            mApiTextureType = vr::TextureType_Metal;
    }
    //-------------------------------------------------------------------------
    OpenVRCompositorListener::~OpenVRCompositorListener()
    {
        if( mCamera )
            mCamera->setVrData( 0 );

        mWorkspace->removeListener( this );
        mRoot->removeFrameListener( this );
    }
    //-------------------------------------------------------------------------
    Ogre::Matrix4 OpenVRCompositorListener::convertSteamVRMatrixToMatrix4( vr::HmdMatrix34_t matPose )
    {
        Ogre::Matrix4 matrixObj(
                    matPose.m[0][0], matPose.m[0][1], matPose.m[0][2], matPose.m[0][3],
                    matPose.m[1][0], matPose.m[1][1], matPose.m[1][2], matPose.m[1][3],
                    matPose.m[2][0], matPose.m[2][1], matPose.m[2][2], matPose.m[2][3],
                               0.0f,            0.0f,            0.0f,            1.0f );
        return matrixObj;
    }
    //-------------------------------------------------------------------------
    Ogre::Matrix4 OpenVRCompositorListener::convertSteamVRMatrixToMatrix4( vr::HmdMatrix44_t matPose )
    {
        Ogre::Matrix4 matrixObj(
                    matPose.m[0][0], matPose.m[0][1], matPose.m[0][2], matPose.m[0][3],
                    matPose.m[1][0], matPose.m[1][1], matPose.m[1][2], matPose.m[1][3],
                    matPose.m[2][0], matPose.m[2][1], matPose.m[2][2], matPose.m[2][3],
                    matPose.m[3][0], matPose.m[3][1], matPose.m[3][2], matPose.m[3][3] );
        return matrixObj;
    }
    //-------------------------------------------------------------------------
    void OpenVRCompositorListener::updateHmdTrackingPose(void)
    {
        mVrCompositor->WaitGetPoses( mTrackedDevicePose, vr::k_unMaxTrackedDeviceCount, NULL, 0 );

        mValidPoseCount = 0;
        for( size_t nDevice = 0; nDevice < vr::k_unMaxTrackedDeviceCount; ++nDevice )
        {
            if ( mTrackedDevicePose[nDevice].bPoseIsValid )
            {
                ++mValidPoseCount;
                mDevicePose[nDevice] = convertSteamVRMatrixToMatrix4(
                                           mTrackedDevicePose[nDevice].mDeviceToAbsoluteTracking );
#if 0
                Ogre::Matrix3 m;
                	m.FromEulerAnglesXYZ(Ogre::Radian(Ogre::Degree(0)), Ogre::Degree(90),
                						 Ogre::Radian(Ogre::Degree(0)));

                mDevicePose[nDevice] = Ogre::Matrix4::getTrans(0,2,0)*m;
#endif
            }
        }

        if( mTrackedDevicePose[vr::k_unTrackedDeviceIndex_Hmd].bPoseIsValid )
        {
            const bool canSync = canSyncCameraTransformImmediately();
            if( canSync )
                syncCamera();
            else
                mMustSyncAtEndOfFrame = true;
        }
    }
    //-------------------------------------------------------------------------
    void OpenVRCompositorListener::syncCullCamera(void)
    {
        const Ogre::Quaternion derivedRot = mCamera->getDerivedOrientation();
        Ogre::Vector3 camPos = mCamera->getDerivedPosition();
        mVrCullCamera->setOrientation( derivedRot );
        mVrCullCamera->setPosition( camPos + derivedRot * mCullCameraOffset );
    }
    //-------------------------------------------------------------------------
    void OpenVRCompositorListener::syncCamera(void)
    {
        OGRE_ASSERT_MEDIUM( mTrackedDevicePose[vr::k_unTrackedDeviceIndex_Hmd].bPoseIsValid );
        mCamera->setPosition( mDevicePose[vr::k_unTrackedDeviceIndex_Hmd].getTrans() );
        mCamera->setOrientation( mDevicePose[vr::k_unTrackedDeviceIndex_Hmd].extractQuaternion() );

        if( mWaitingMode < VrWaitingMode::AfterFrustumCulling )
            syncCullCamera();

        mMustSyncAtEndOfFrame = false;
    }
    //-------------------------------------------------------------------------
    void OpenVRCompositorListener::syncCameraProjection( bool bForceUpdate )
    {
        const Ogre::Real camNear = mCamera->getNearClipDistance();
        const Ogre::Real camFar  = mCamera->getFarClipDistance();

        if( mLastCamNear != camNear || mLastCamFar != camFar || bForceUpdate )
        {
            Ogre::Matrix4 eyeToHead[2];
            Ogre::Matrix4 projectionMatrix[2];
            Ogre::Matrix4 projectionMatrixRS[2];
            Ogre::Vector4 eyeFrustumExtents[2];

            for( size_t i=0u; i<2u; ++i )
            {
                vr::EVREye eyeIdx = static_cast<vr::EVREye>( i );
                eyeToHead[i] = convertSteamVRMatrixToMatrix4( mHMD->GetEyeToHeadTransform( eyeIdx ) );
                //eyeToHead[i] =Ogre::Matrix4::IDENTITY;


                projectionMatrix[i] =
                        convertSteamVRMatrixToMatrix4( mHMD->GetProjectionMatrix( eyeIdx,
                                                                                  camNear, camFar ) );
                mRenderSystem->_convertOpenVrProjectionMatrix( projectionMatrix[i],
                                                               projectionMatrixRS[i] );

                std::cout<<i<<" "<< projectionMatrix[i]<< std::endl;
                std::cout<<i<<" "<< eyeToHead[i]<< std::endl;
                mHMD->GetProjectionRaw( eyeIdx, &eyeFrustumExtents[i].x, &eyeFrustumExtents[i].y,
                                        &eyeFrustumExtents[i].z, &eyeFrustumExtents[i].w );

#if 1
                mVrData.mProjInverse[i]=projectionMatrix[i].inverse();
                {
                	Ogre::Matrix4 invProj = projectionMatrix[i].inverse();
                    Ogre::Vector4 topLeft( -1.0f, 1.0f, -1.0f, 1.0f );
                    Ogre::Vector4 bottomRight( 1.0f, -1.0f, -1.0f, 1.0f );

                    topLeft = invProj * topLeft;
                    bottomRight = invProj * bottomRight;

                    float left = (topLeft.x / topLeft.w)*10.0f;
                    float top = (topLeft.y / topLeft.w)*10.0f;
                    float right = (bottomRight.x / bottomRight.w)*10.0f;
                    float bottom = (bottomRight.y / bottomRight.w)*10.0f;

                    if (i==1)
                    {
                    	printf("eye %f %f\n",eyeToHead[0][0][3],eyeToHead[1][0][3]);
                    	printf("eye %f %f\n",eyeToHead[0][1][3],eyeToHead[1][1][3]);
                    	printf("eye %f %f\n",eyeToHead[0][2][3],eyeToHead[1][2][3]);
                    	//bottom-=eyeToHead[0][1][3];
                    	//top-=eyeToHead[0][1][3];
                    }
                    else
                    {

                    }
                    //bottom-=eyeToHead[i][2][3]/2;
                    //top-=eyeToHead[i][2][3]/2;

                    mCamera->setVrFrustumExtents( i,left,right,top,bottom,Ogre::FET_TAN_HALF_ANGLES );

                    printf("Calc %d %f %f %f %f\n",i,left,right,top,bottom);

                }
#endif
#if 0
                if (i==1)
                {
                	eyeFrustumExtents[i].z+=0.0024;
                	eyeFrustumExtents[i].w+=0.0024;
                }
                else

                	                {
                	                	eyeFrustumExtents[i].z-=0.0024;
                	                	eyeFrustumExtents[i].w-=0.0024;
                	                }
#endif
#if 0
                mCamera->setVrFrustumExtents( i,eyeFrustumExtents[i].x, eyeFrustumExtents[i].y,
                            		eyeFrustumExtents[i].w, eyeFrustumExtents[i].z,
                                                              Ogre::FET_TAN_HALF_ANGLES );
#endif
            }

            printf("Offset %f\n", eyeFrustumExtents[0].z-eyeFrustumExtents[1].z);
            printf("Offset %f\n", eyeFrustumExtents[0].w-eyeFrustumExtents[1].w);

            printf("Size %f\n", eyeFrustumExtents[0].z-eyeFrustumExtents[0].w);
            printf("Size %f\n", eyeFrustumExtents[1].z-eyeFrustumExtents[1].w);

            mVrData.set( eyeToHead, projectionMatrixRS );

#if 1
            printf("DDD %f %f %f\n",mVrData.mLeftToRight[0],mVrData.mLeftToRight[1],mVrData.mLeftToRight[2]);

            size_t i=0;

            printf("DDD %f %f %f %f\n",eyeFrustumExtents[i].x, eyeFrustumExtents[i].y,
            		eyeFrustumExtents[i].w, eyeFrustumExtents[i].z);
            i=1;
            printf("DDD %f %f %f %f\n",eyeFrustumExtents[i].x, eyeFrustumExtents[i].y,
                        		eyeFrustumExtents[i].w, eyeFrustumExtents[i].z);
#endif

            mLastCamNear = camNear;
            mLastCamFar = camFar;

            Ogre::Vector4 cameraCullFrustumExtents;
            cameraCullFrustumExtents.x = std::min( eyeFrustumExtents[0].x, eyeFrustumExtents[1].x );
            cameraCullFrustumExtents.y = std::max( eyeFrustumExtents[0].y, eyeFrustumExtents[1].y );
            cameraCullFrustumExtents.z = std::max( eyeFrustumExtents[0].z, eyeFrustumExtents[1].z );
            cameraCullFrustumExtents.w = std::min( eyeFrustumExtents[0].w, eyeFrustumExtents[1].w );

            mVrCullCamera->setFrustumExtents( cameraCullFrustumExtents.x, cameraCullFrustumExtents.y,
                                              cameraCullFrustumExtents.w, cameraCullFrustumExtents.z,
                                              Ogre::FET_TAN_HALF_ANGLES );

            const float ipd = mVrData.mLeftToRight.x;
            mCullCameraOffset = Ogre::Vector3::ZERO;
            mCullCameraOffset.z = (ipd / 2.0f) / Ogre::Math::Abs( cameraCullFrustumExtents.x );

            const Ogre::Real offset = mCullCameraOffset.length();
            mVrCullCamera->setNearClipDistance( camNear + offset );
            mVrCullCamera->setFarClipDistance( camFar + offset );
        }
    }
    //-------------------------------------------------------------------------
    bool OpenVRCompositorListener::frameStarted( const Ogre::FrameEvent& evt )
    {
        if( mWaitingMode == VrWaitingMode::BeforeSceneGraph )
            updateHmdTrackingPose();
        return true;
    }
    //-------------------------------------------------------------------------
    bool OpenVRCompositorListener::frameRenderingQueued( const Ogre::FrameEvent &evt )
    {
        vr::VRTextureBounds_t texBounds;
        if( mVrTexture->requiresTextureFlipping() )
        {
            texBounds.vMin = 1.0f;
            texBounds.vMax = 0.0f;
        }
        else
        {
            texBounds.vMin = 0.0f;
            texBounds.vMax = 1.0f;
        }

        vr::Texture_t eyeTexture =
        {
            0,
            mApiTextureType,
            vr::ColorSpace_Gamma
        };
        mVrTexture->getCustomAttribute( Ogre::TextureGpu::msFinalTextureBuffer, &eyeTexture.handle );

        texBounds.uMin = 0;
        texBounds.uMax = 0.5f;
        mVrCompositor->Submit( vr::Eye_Left, &eyeTexture, &texBounds );
        texBounds.uMin = 0.5f;
        texBounds.uMax = 1.0f;
        mVrCompositor->Submit( vr::Eye_Right, &eyeTexture, &texBounds );

        mRenderSystem->flushCommands();


        vr::VREvent_t event;
        while( mHMD->PollNextEvent( &event, sizeof(event) ) )
        {
            if( event.trackedDeviceIndex != vr::k_unTrackedDeviceIndex_Hmd &&
                event.trackedDeviceIndex != vr::k_unTrackedDeviceIndexInvalid )
            {
                continue;
            }

            switch( event.eventType )
            {
            case vr::VREvent_TrackedDeviceUpdated:
            case vr::VREvent_IpdChanged:
            case vr::VREvent_ChaperoneDataHasChanged:
                syncCameraProjection( true );
                break;
            }
        }

        return true;
    }
    //-------------------------------------------------------------------------
    bool OpenVRCompositorListener::frameEnded( const Ogre::FrameEvent& evt )
    {
        syncCameraProjection( false );
        if( mWaitingMode == VrWaitingMode::AfterSwap )
            updateHmdTrackingPose();
        else
            mVrCompositor->PostPresentHandoff();
        if( mMustSyncAtEndOfFrame )
            syncCamera();
        if( mWaitingMode >= VrWaitingMode::AfterFrustumCulling )
            syncCullCamera();
        return true;
    }
    //-------------------------------------------------------------------------
    void OpenVRCompositorListener::workspacePreUpdate( Ogre::CompositorWorkspace *workspace )
    {
        if( mWaitingMode == VrWaitingMode::AfterSceneGraph )
            updateHmdTrackingPose();
    }
    //-------------------------------------------------------------------------
    void OpenVRCompositorListener::passPreExecute( Ogre::CompositorPass *pass )
    {
        if( mWaitingMode == VrWaitingMode::BeforeShadowmaps &&
            pass->getDefinition()->getType() == Ogre::PASS_SCENE &&
            pass->getDefinition()->mIdentifier == 0x01234567 )
        {
            updateHmdTrackingPose();
        }
    }
    //-------------------------------------------------------------------------
    void OpenVRCompositorListener::passSceneAfterShadowMaps( Ogre::CompositorPassScene *pass )
    {
        if( mWaitingMode == VrWaitingMode::BeforeFrustumCulling &&
            pass->getDefinition()->mIdentifier == 0x01234567 )
        {
            updateHmdTrackingPose();
        }
    }
    //-------------------------------------------------------------------------
    void OpenVRCompositorListener::passSceneAfterFrustumCulling( Ogre::CompositorPassScene *pass )
    {
        if( mWaitingMode == VrWaitingMode::AfterFrustumCulling &&
            pass->getDefinition()->mIdentifier == 0x01234567 )
        {
            updateHmdTrackingPose();
        }
    }
    //-------------------------------------------------------------------------
    void OpenVRCompositorListener::setWaitingMode( VrWaitingMode::VrWaitingMode waitingMode )
    {
        mWaitingMode = waitingMode;
    }
    //-------------------------------------------------------------------------
    void OpenVRCompositorListener::setGlitchFree( VrWaitingMode::VrWaitingMode firstGlitchFreeMode )
    {
        mFirstGlitchFreeMode = firstGlitchFreeMode;
    }
    //-------------------------------------------------------------------------
    bool OpenVRCompositorListener::canSyncCameraTransformImmediately(void) const
    {
        return mWaitingMode <= VrWaitingMode::BeforeSceneGraph ||
               mWaitingMode <= mFirstGlitchFreeMode;
    }
}
