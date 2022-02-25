#include <rviz/mesh_loader.h>
#include <ros/console.h>
#include <ros/package.h>
#include <resource_retriever/retriever.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>

#include <OgreSceneManager.h>
#include <OgreSubEntity.h>
#include <OgreMaterialManager.h>
#include <OgreTextureManager.h>
#include <OgreTechnique.h>
#include <OgreAnimation.h>

#include "person_visual.h"


namespace fs = boost::filesystem;

namespace framesoft_tracking_rviz_plugin {

/** This helper class ensures that skeletons can be loaded from a package:// path **/
class RosPackagePathResourceLoadingListener : public Ogre::ResourceLoadingListener
{
public:
    RosPackagePathResourceLoadingListener(const fs::path& parentPath) : _parentPath(parentPath) {
    }

    /** This event is called when a resource beings loading. */
    virtual Ogre::DataStreamPtr resourceLoading(const Ogre::String &name, const Ogre::String &group, Ogre::Resource *resource) {
      fs::path absolutePath = _parentPath / name;
      ROS_INFO_STREAM("RosPackagePathResourceLoadingListener loading resource: " << absolutePath.string());

      try
      {
        resource_retriever::Retriever retriever;
        _lastResource = retriever.get(absolutePath.string()); // not thread-safe!
        return Ogre::DataStreamPtr(new Ogre::MemoryDataStream(_lastResource.data.get(), _lastResource.size));
      }
      catch (resource_retriever::Exception& e)
      {
        ROS_ERROR("In RosPackagePathResourceLoadingListener: %s", e.what());
        return Ogre::DataStreamPtr();
      }
    }

    virtual void resourceStreamOpened(const Ogre::String &name, const Ogre::String &group, Ogre::Resource *resource, Ogre::DataStreamPtr& dataStream) {
    }

    virtual bool resourceCollision(Ogre::Resource *resource, Ogre::ResourceManager *resourceManager) {
      return false;
    }

private:
    const fs::path& _parentPath;
    resource_retriever::MemoryResource _lastResource;
};



MeshPersonVisual::MeshPersonVisual(const PersonVisualDefaultArgs& args) : PersonVisual(args), m_animationState(NULL), m_walkingSpeed(1.0), entity_(NULL)
{
    m_childSceneNode = m_sceneNode->createChildSceneNode();
    m_childSceneNode->setVisible(false);

    std::string meshResource = "package://" ROS_PACKAGE_NAME "/media/animated_walking_man.mesh";

    /// This is required to load referenced skeletons from package:// path
    fs::path model_path(meshResource);
    fs::path parent_path(model_path.parent_path());

    Ogre::ResourceLoadingListener *newListener = new RosPackagePathResourceLoadingListener(parent_path), 
                                  *oldListener = Ogre::ResourceGroupManager::getSingleton().getLoadingListener();

    Ogre::ResourceGroupManager::getSingleton().setLoadingListener(newListener);
    bool loadFailed = rviz::loadMeshFromResource(meshResource).isNull();
    Ogre::ResourceGroupManager::getSingleton().setLoadingListener(oldListener);

    delete newListener;


    // Create scene entity
    static size_t count = 0;
    std::stringstream ss;
    ss << "mesh_person_visual" << count++;
    std::string id = ss.str();

    entity_ = m_sceneManager->createEntity(id, meshResource);
    m_childSceneNode->attachObject(entity_);

    // set up animation
    setAnimationState("");

    // set up material
    ss << "Material";
    Ogre::MaterialPtr default_material = Ogre::MaterialManager::getSingleton().create( ss.str(), "rviz" );
    default_material->setReceiveShadows(false);
    default_material->getTechnique(0)->setLightingEnabled(true);
    default_material->getTechnique(0)->setAmbient( 0.5, 0.5, 0.5 );
    materials_.insert( default_material );
    entity_->setMaterial( default_material );

    // set position
    Ogre::Quaternion quat1; quat1.FromAngleAxis(Ogre::Degree(90), Ogre::Vector3(0,1,0));
    Ogre::Quaternion quat2; quat2.FromAngleAxis(Ogre::Degree(-90), Ogre::Vector3(0,0,1));
    m_childSceneNode->setOrientation(quat1 * quat2);

    double scaleFactor = 0.243 * 1.75;
    m_childSceneNode->setScale(Ogre::Vector3(scaleFactor, scaleFactor, scaleFactor));
    m_childSceneNode->setPosition(Ogre::Vector3(0, 0, -1));

    m_childSceneNode->setVisible(true);
}

MeshPersonVisual::~MeshPersonVisual() {
    m_sceneManager->destroyEntity( entity_ );

    // destroy all the materials we've created
    std::set<Ogre::MaterialPtr>::iterator it;
    for ( it = materials_.begin(); it!=materials_.end(); it++ )
    {
        Ogre::MaterialPtr material = *it;
        if (!material.isNull())
        {
          material->unload();
          Ogre::MaterialManager::getSingleton().remove(material->getName());
        }
    }
    materials_.clear();

    m_sceneManager->destroySceneNode(m_childSceneNode->getName());
}

void MeshPersonVisual::setColor(const Ogre::ColourValue& c) {
    Ogre::SceneBlendType blending;
    bool depth_write;

    if ( c.a < 0.9998 )
    {
      blending = Ogre::SBT_TRANSPARENT_ALPHA;
      depth_write = false;
    }
    else
    {
      blending = Ogre::SBT_REPLACE;
      depth_write = true;
    }

    std::set<Ogre::MaterialPtr>::iterator it;
    for( it = materials_.begin(); it != materials_.end(); it++ )
    {
      Ogre::Technique* technique = (*it)->getTechnique( 0 );

      technique->setAmbient( c.r*0.5, c.g*0.5, c.b*0.5 );
      technique->setDiffuse( c.r, c.g, c.b, c.a );
      technique->setSceneBlending( blending );
      technique->setDepthWriteEnabled( depth_write );
      technique->setLightingEnabled( true );
    }
}

void MeshPersonVisual::setAnimationState(const std::string& nameOfAnimationState) {
    Ogre::AnimationStateSet *animationStates = entity_->getAllAnimationStates();
    if(animationStates != NULL)
    {
      Ogre::AnimationStateIterator animationsIterator = animationStates->getAnimationStateIterator();
      while (animationsIterator.hasMoreElements())
      {
        Ogre::AnimationState *animationState = animationsIterator.getNext();
        if(animationState->getAnimationName() == nameOfAnimationState || nameOfAnimationState.empty()) {
          animationState->setLoop(true);
          animationState->setEnabled(true);
          m_animationState = animationState;
          return;
        }    
      }

      // Not found. Set first animation state then.
      ROS_WARN_STREAM_ONCE("Person mesh animation state " << nameOfAnimationState << " does not exist in mesh!");
      setAnimationState("");
    }
}

void MeshPersonVisual::setWalkingSpeed(float walkingSpeed) {
    m_walkingSpeed = walkingSpeed;
}


void MeshPersonVisual::update(float deltaTime) {
    if(m_animationState) {
        m_animationState->addTime(0.7 * deltaTime * m_walkingSpeed);
    }
}


} // end of namespace framesoft_tracking_rviz_plugin