#include "cinder/app/AppNative.h"
#include "cinder/app/AppScreenSaver.h"
#include "cinder/Cinder.h"
#include "cinder/Color.h"
#include "cinder/Filesystem.h"
#include "cinder/gl/Fbo.h"
#include "cinder/gl/gl.h"
#include "cinder/gl/Texture.h"
#include "cinder/ImageIo.h"
#include "cinder/Rand.h"
#include "cinder/Surface.h"
#include "cinder/Utilities.h"

#include <functional>
#include <sstream>
#include <vector>

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

#define FRAMERATE            60.0f
#define VIDEO_FRAMERATE      30.0f

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////


#if defined( CINDER_MAC ) && ( MAC_OS_X_VERSION_MIN_REQUIRED <= MAC_OS_X_VERSION_10_7 )
#error "Mac OS 10.7 requires garbage collection for screensavers. Make sure you have rebuilt Cinder with GCC_ENABLE_OBJC_GC set to 'supported' and then delete this message."
#endif

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

class WavyWaves;

#if defined BUILD_AS_APP
typedef ci::app::AppNative BaseApp;
#else
typedef ci::app::AppScreenSaver BaseApp;
#endif

class CinderApp : public BaseApp
{
public:
  // setup
  void prepareSettings( BaseApp::Settings *settings );
  void setup();
  
  // main routines
  void update();
  void draw();
  
  // events
  void keyDown( ci::app::KeyEvent _event );

  // properties
  ci::fs::path              m_vidPath;
  long                      m_currentFrame;

  std::vector< WavyWaves* > m_wavers;

  static double             s_currentTime;
  static double             s_lastTime;
  static double             s_delta;
};

double CinderApp::s_currentTime = 0.0;
double CinderApp::s_lastTime    = 0.0;
double CinderApp::s_delta       = 0.0;

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

class WavyWaves
{
public:
  WavyWaves( ci::app::WindowRef _window ) : m_window( _window ), m_windowSize( _window->getSize() ) {}
  
  // setup
  void setup();
  
  // main routines
  void update( double _delta );
  void draw();
    
  // type definition to make things easier on position definition
  typedef std::function< float ( float ) > PosFunc;
  
  // properties
  ci::app::WindowRef          m_window;
  ci::Vec2f                   m_windowSize;
  ci::gl::Fbo                 m_frameBufferObject;
  std::vector< ci::Vec4f >    m_particles; // pos( x, y ), vel( z, w )
  float                       m_particleCount;
    
  std::vector< PosFunc >       m_fn;
  
  PosFunc                      m_xfn[ 2 ];
  PosFunc                      m_yfn[ 2 ];
  
  double                      m_xFuncTimer;
  double                      m_yFuncTimer;
  double                      m_xFuncTimeout;
  double                      m_yFuncTimeout;
  
  static float                s_speedFactor;
  static float                s_minChangeTime;
  static float                s_maxChangeTime;
};


float WavyWaves::s_speedFactor   = 4.0f;
float WavyWaves::s_minChangeTime = 20.0f;
float WavyWaves::s_maxChangeTime = 60.0f;

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

void WavyWaves::setup()
{  
  // positioning functions
  m_fn.push_back( &ci::math< float >::sin );
  m_fn.push_back( &ci::math< float >::cos );
  m_fn.push_back( &ci::math< float >::tan );
  m_fn.push_back( [ this ]( float x ){ return 1.0f; } );
  m_fn.push_back( [ this ]( float x ){ return m_particleCount / ( x * x ); } );
  m_fn.push_back( [ this ]( float x ){ return ci::math< float >::cos( static_cast< float >( CinderApp::s_currentTime / 1e11 ) ); } );
  m_fn.push_back( [ this ]( float x ){ return 5.0f / ci::math< float >::fmod( x, 5.0f ); } );
  m_fn.push_back( [ this ]( float x ){ return m_particleCount / ( x * 25.0f - m_particleCount); } );
  m_fn.push_back( [ this ]( float x ){ return m_particleCount / ( x * 25.0f - m_windowSize.x / 2.0f ); } );
  m_fn.push_back( [ this ]( float x ){ return ci::math< float >::fmod( x, 4.0f ) > 2.0f ? 1.0f : -1.0f; } );
  m_fn.push_back( [ this ]( float x ){ return 1 / ci::math< float >::sin( x ); } );
  m_fn.push_back( [ this ]( float x ){ return ci::math< float >::sin( x )*ci::math< float >::tan( x / 5 ); } );
  m_fn.push_back( [ this ]( float x ){ return ci::math< float >::sin( x ) * m_fn[ 9 ]( x ); } );

  
  // initialize the x and y functions
  m_xfn[ 0 ] = m_fn[ ci::Rand::randUint( m_fn.size() ) ];
  m_yfn[ 0 ] = m_fn[ ci::Rand::randUint( m_fn.size() ) ];
  m_xfn[ 1 ] = m_fn[ ci::Rand::randUint( m_fn.size() ) ];
  m_yfn[ 1 ] = m_fn[ ci::Rand::randUint( m_fn.size() ) ];

  
  // initialize the function timeouts 
  m_xFuncTimer   = 0.0;
  m_yFuncTimer   = 0.0;
  m_xFuncTimeout = ci::Rand::randFloat( WavyWaves::s_minChangeTime, WavyWaves::s_maxChangeTime );
  m_yFuncTimeout = ci::Rand::randFloat( WavyWaves::s_minChangeTime, WavyWaves::s_maxChangeTime );
  
  // buffer for trails
  ci::Vec2i displaySz = m_window->getSize();
  
  m_frameBufferObject = ci::gl::Fbo( displaySz.x, displaySz.y, true );
  m_frameBufferObject.bindFramebuffer();

  ci::gl::enableAlphaBlending();
  ci::gl::clear( ci::Color( 0.0f, 0.0f, 0.0f ) ); 

  m_frameBufferObject.unbindFramebuffer();
  
  // enables alpha blending for the main GL buffer
  ci::gl::enableAlphaBlending();

  // stores the window size to use later
  m_windowSize = displaySz;
  
  m_particleCount = static_cast< float >( displaySz.x * displaySz.y / 810 );
  while ( m_particles.size() < static_cast< size_t >( m_particleCount ) )
  {
    m_particles.push_back( 
      ci::Vec4f( 
        ci::Rand::randFloat( static_cast< float >( displaySz.x ) ), 
        ci::Rand::randFloat( static_cast< float >( displaySz.y ) ),
        0,
        0
      )
    );
  }
}

////////////////////////////////////////////////////////////////////////////////

void WavyWaves::update( double _delta )
{
  // add the times to the timer
  m_xFuncTimer = m_xFuncTimer + _delta;
  m_yFuncTimer = m_yFuncTimer + _delta;
  
  // calculates the percentual of each function
  float xPercent    = ci::math< float >::clamp( static_cast< float >( m_xFuncTimer / m_xFuncTimeout ) );
  float xInvPercent = 1.0f - xPercent;
  float yPercent    = ci::math< float >::clamp( static_cast< float >( m_yFuncTimer / m_yFuncTimeout ) );
  float yInvPercent = 1.0f - yPercent;
  
  // update the particles
  for ( auto& p : m_particles )
  {
    // update the position and velocity of each particle
    p.z = ( m_xfn[ 0 ]( p.y / 25.0f ) * xInvPercent + m_xfn[ 1 ]( p.y / 25.0f ) * xPercent - 0.5f ) * static_cast< float >( _delta ) * ( FRAMERATE / WavyWaves::s_speedFactor );
    p.w = ( m_yfn[ 0 ]( p.x / 25.0f ) * yInvPercent + m_yfn[ 1 ]( p.x / 25.0f ) * yPercent - 0.5f ) * static_cast< float >( _delta ) * ( FRAMERATE / WavyWaves::s_speedFactor );
    p.x = ci::math< float >::fmod( p.x + m_windowSize.x + p.z, m_windowSize.x );
    p.y = ci::math< float >::fmod( p.y + m_windowSize.y + p.w, m_windowSize.y );
  }
  
  // resets the timers if needed
  if ( m_xFuncTimer >= m_xFuncTimeout )
  {
    m_xFuncTimer   = 0.0;
    m_xFuncTimeout = ci::Rand::randFloat( WavyWaves::s_minChangeTime, WavyWaves::s_maxChangeTime );
    m_xfn[ 0 ]     = m_xfn[ 1 ];
    m_xfn[ 1 ]     = m_fn[ ci::Rand::randUint( m_fn.size() ) ];
  }

  if ( m_yFuncTimer >= m_yFuncTimeout )
  {
    m_yFuncTimer   = 0.0;
    m_yFuncTimeout = ci::Rand::randFloat( WavyWaves::s_minChangeTime, WavyWaves::s_maxChangeTime );
    m_yfn[ 0 ]     = m_yfn[ 1 ];
    m_yfn[ 1 ]     = m_fn[ ci::Rand::randUint( m_fn.size() ) ];
  }
}

////////////////////////////////////////////////////////////////////////////////

void WavyWaves::draw()
{  
  ci::gl::pushMatrices();
  ci::gl::setMatricesWindow(m_window->getSize());
  ci::gl::disableDepthRead();
  ci::gl::disableDepthWrite();

  // writes backed up frame buffer to the screen
  m_frameBufferObject.blitToScreen( m_window->getBounds(), m_window->getBounds() );

  ci::gl::enableAlphaBlending();

  // darkens the BG
  ci::gl::color( 0.0f, 0.0f, 0.0f, 0.01f ); 
  ci::gl::drawSolidRect( m_window->getBounds() );

  // do the drawing =D
  for ( auto p : m_particles )
  {
    ci::gl::color( 
      ci::math< float >::clamp( p.x / m_windowSize.x ),
      ci::math< float >::clamp( ( p.z * p.z + p.w * p.w ) * WavyWaves::s_speedFactor ),
      ci::math< float >::clamp( p.y / m_windowSize.y ),
      1.0f ); 
      
     ci::gl::drawSolidCircle( ci::Vec2f( p.x, p.y ), 5.0f / ci::math< float >::max( ( p.z * p.z + p.w * p.w ) * WavyWaves::s_speedFactor, 0.5f ) );
  }

  ci::gl::disableAlphaBlending();
  ci::gl::enableDepthRead();
  ci::gl::enableDepthWrite();
  ci::gl::popMatrices();

  // save what happened to the framebuffer
  m_frameBufferObject.blitFromScreen( m_window->getBounds(), m_window->getBounds() );  
}

////////////////////////////////////////////////////////////////////////////////
  
void CinderApp::prepareSettings( BaseApp::Settings *settings )
{
#if defined BUILD_AS_APP
  // set the window size and etc.
  settings->setWindowSize( 800, 600 );
  settings->setBorderless( true );
#endif
  settings->setFullScreen( false );
  settings->setFrameRate( FRAMERATE );
}
////////////////////////////////////////////////////////////////////////////////

void CinderApp::setup()
{
  // timers
  CinderApp::s_lastTime = CinderApp::s_currentTime = ci::app::getElapsedSeconds();
  m_currentFrame = -1;

  // create wavers
#if defined BUILD_AS_APP
  m_wavers.push_back( new WavyWaves( getWindow() ) );
  m_wavers[ 0 ]->setup();
#else
  for ( size_t windowIndex = 0; windowIndex < getNumWindows(); ++windowIndex )
  {
    m_wavers.push_back( new WavyWaves( getWindowIndex( windowIndex ) ) );
    m_wavers[ windowIndex ]->setup();
  }
#endif
}
  
////////////////////////////////////////////////////////////////////////////////

void CinderApp::update()
{
  // set the timers
  if ( m_currentFrame != -1 ) // capturing video - renders constant framerate
  {
    CinderApp::s_delta        = 1.0 / VIDEO_FRAMERATE;
    CinderApp::s_currentTime += CinderApp::s_delta;
  }
  else // not capturing, render whatever it goes
  {
    CinderApp::s_delta        = 1.0 / FRAMERATE;
    CinderApp::s_currentTime += CinderApp::s_delta;
  }
    
  // call the update for each waver
  for ( auto waver : m_wavers )
  {
    waver->update( CinderApp::s_delta );
  }

  // update the last time timer
  CinderApp::s_lastTime = CinderApp::s_currentTime;
}

////////////////////////////////////////////////////////////////////////////////

void CinderApp::draw()
{
  // call the draw for each waver
  for ( auto waver : m_wavers )
  {
    waver->draw();
  }

  // captures the video
  if ( m_currentFrame != -1 ) 
  {
     ci::writeImage( m_vidPath / ( ci::toString( m_currentFrame ) + ".jpg" ), m_wavers[ 0 ]->m_frameBufferObject.getTexture() );        
     m_currentFrame++;
  }
}

////////////////////////////////////////////////////////////////////////////////

void CinderApp::keyDown( ci::app::KeyEvent _event )
{
#if defined BUILD_AS_APP
  switch( _event.getChar() ) 
  {        
    case 'c': 
    {
      if ( m_currentFrame == -1 )
      {
        m_currentFrame   = 0;
        size_t vidNumber = 0;
        
        while ( true )
        {            
          m_vidPath = ci::getDocumentsDirectory() / ( "FlockDrawCapture_" + ci::toString( vidNumber ) );
         
          if ( !ci::fs::exists( m_vidPath ) )
          {
            ci::fs::create_directories( m_vidPath );
            break;
          }
          ++vidNumber;
        }
      }
      else // ends capture
      {
        m_currentFrame = -1;
      }
    }
    break;
  }
#endif

  switch( _event.getCode() ) 
  {
  case ci::app::KeyEvent::KEY_ESCAPE: 
    {
      quit(); 
    }
    break;
  }
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

#if defined BUILD_AS_APP
CINDER_APP_NATIVE( CinderApp, ci::app::RendererGl )
#else
CINDER_APP_SCREENSAVER( CinderApp, ci::app::RendererGl )
#endif
