#include <QAction>
#include <QApplication>
#include <QFileDialog>
#include <QKeyEvent>
#include <QMainWindow>
#include <QMdiArea>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QOpenGLWidget>
#include <QScreen>
#include <QMdiSubWindow>
#include <QSurfaceFormat>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWindow>

#include <osg/Camera>
#include <osg/Geometry>
#include <osg/Geode>
#include <osg/Group>
#include <osg/LineWidth>
#include <osg/Image>
#include <osg/Texture2D>
#include <osg/FrameBufferObject>
#include <osg/GraphicsContext>
#include <osg/Matrix>
#include <osg/Depth>
#include <osg/ShapeDrawable>
#include <osg/CopyOp>
#include <osgDB/WriteFile>
#include <osgGA/TrackballManipulator>
#include <osgText/Text>
#include <osgViewer/Viewer>

#include <iostream>

class OSGWidget;

class OSGWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit	OSGWindow(QWidget* parent=nullptr);
		~OSGWindow();

    void setOSGWidget(OSGWidget*);

private:
    QSize currentScreenDPI() const;

    OSGWidget*	osgwidget_	= nullptr;

private slots:
    void onOpen();
    void onSave();
    void onTakeScreenshot();
    void onExit();

};


class OSGWidget : public QOpenGLWidget {
    Q_OBJECT

public:
    OSGWidget(QWidget* parent=nullptr);
    ~OSGWidget();

    bool doScreenShot(const QSize& dpi,double scalefactor,const char* destpath,
                      std::string& msg) const;

protected:
    void initializeGL() override;
    void resizeGL(int w,int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;

private:
    osgGA::GUIEventAdapter::MouseButtonMask mapQtMouseButton(Qt::MouseButton);
    int mapQtKey(QKeyEvent*);
    void updateLabelAlignment();

    osg::ref_ptr<osgViewer::Viewer> viewer_;
    osg::ref_ptr<osgViewer::GraphicsWindowEmbedded> gw_;
    osg::ref_ptr<osg::Group> root_;
    osg::ref_ptr<osgText::Text> labeltext_;
    osg::Vec3 cubecenter_ = osg::Vec3(0.f, 0.f, 0.f);
    osg::Vec3 labelanchor_ = osg::Vec3(0.f, 0.f, 0.f);
    QTimer* rendertimer_	= nullptr;
    osg::Matrix lastviewmatrix_;
    int stillframecount_	= 0;
    const int maxstillframes_ = 10;
    const int rendertimems_ = 16;
};

#include "od_osgtestapp.moc"


// OSGWindow

OSGWindow::OSGWindow(QWidget* parent)
    : QMainWindow(parent)
{
    auto* menuBar = new QMenuBar;
    QMenu* fileMenu = menuBar->addMenu("&File");

    QAction* openact = fileMenu->addAction("Open...");
    QAction* saveact = fileMenu->addAction("Save...");
    QAction* screenshotact = fileMenu->addAction("Screenshot...");
    fileMenu->addSeparator();
    QAction* exitact = fileMenu->addAction("Exit");

    connect( openact, &QAction::triggered, this, &OSGWindow::onOpen );
    connect( saveact, &QAction::triggered, this, &OSGWindow::onSave );
    connect( screenshotact, &QAction::triggered, this, &OSGWindow::onTakeScreenshot );
    connect( exitact, &QAction::triggered, this, &OSGWindow::onExit );

    setMenuBar( menuBar );
}


OSGWindow::~OSGWindow()
{}


void OSGWindow::setOSGWidget( OSGWidget* osgwidget )
{
     osgwidget_ = osgwidget;
}


QSize OSGWindow::currentScreenDPI() const
{
    QScreen* targetScreen = nullptr;

    if ( windowHandle() )
	targetScreen = windowHandle()->screen();
    if ( !targetScreen )
	targetScreen = screen();
    if ( !targetScreen )
	targetScreen = QGuiApplication::primaryScreen();

    if ( !targetScreen )
	return QSize( 94., 94. );

    // Physical DPI corresponds to the real monitor density.
    return QSize( targetScreen->physicalDotsPerInchX(),
		  targetScreen->physicalDotsPerInchY() );
}


void OSGWindow::onOpen()
{
    const QString path = QFileDialog::getOpenFileName( this, "Open File" );
    if ( !path.isEmpty() )
    {
	QMessageBox::information( this, "File Opened",
				  "You selected:\n" + path );
	// TODO: load file into OSGWidget
    }
}


void OSGWindow::onSave()
{
    const QString path = QFileDialog::getSaveFileName( this, "Save File" );
    if (!path.isEmpty())
    {
	QMessageBox::information( this, "File Saved", "Saved to:\n" + path );
	// TODO: save OSG scene
    }
}


void OSGWindow::onTakeScreenshot()
{
    if ( !osgwidget_ )
    {
	QMessageBox::critical( this, "Error", "No viewer available" );
	return;
    }

    const double scalefactor = 2.;
    const QString outpath = QDir(QDir::tempPath()).filePath("screenshot.png");
    std::string msg;
    if ( !osgwidget_->doScreenShot(currentScreenDPI(),scalefactor,outpath.toStdString().c_str(),msg) )
    {
	QMessageBox::critical( this, "Error", msg.c_str() );
	return;
    }

    QMessageBox::information( this, "Screenshot", msg.c_str() );
}


void OSGWindow::onExit()
{
    close(); // closes the window
}


// OSGWidget

OSGWidget::OSGWidget( QWidget* parent )
    : QOpenGLWidget(parent)
{
    setMinimumSize( 600, 400 );
    setFocusPolicy( Qt::StrongFocus );
    setMouseTracking( true );
    setUpdateBehavior( QOpenGLWidget::PartialUpdate );
    setAttribute( Qt::WA_OpaquePaintEvent );
    setAttribute( Qt::WA_NoSystemBackground );

    viewer_ = new osgViewer::Viewer;
    viewer_->setThreadingModel( osgViewer::Viewer::SingleThreaded );

    rendertimer_ = new QTimer(this);
    connect(rendertimer_, &QTimer::timeout, this, [&]() {
	    update();
    });

    rendertimer_->start( rendertimems_ );
}


OSGWidget::~OSGWidget()
{
    auto* osgwin = dynamic_cast<OSGWindow*>( QApplication::activeWindow() );
    if ( osgwin )
	osgwin->setOSGWidget( nullptr );
}


void OSGWidget::initializeGL()
{
    // Connect OSG graphics context to Qt's native window
    osg::ref_ptr<osg::GraphicsContext::Traits> traits =
					new osg::GraphicsContext::Traits;
    traits->x = 0;
    traits->y = 0;
    traits->width = this->width();
    traits->height = this->height();
    traits->windowDecoration = false;
    traits->doubleBuffer = true;
    traits->sharedContext = nullptr;

    gw_ = new osgViewer::GraphicsWindowEmbedded( traits->x, traits->y,
						 traits->width, traits->height);

    osg::ref_ptr<osg::Camera> camera = viewer_->getCamera();
    camera->setGraphicsContext( gw_.get() );
    camera->setViewport( new osg::Viewport(0, 0, traits->width,traits->height) );
    camera->setProjectionMatrixAsPerspective( 30., double(width()) / height(),
					      1., 10000.);
    camera->setClearColor( osg::Vec4(0.2f, 0.2f, 0.2f, 1.f) );
			   // Nice neutral gray

    // Build a scene with a cube, a line and a label
    root_ = new osg::Group;
    osg::ref_ptr<osg::Geode> geode = new osg::Geode;

    // Cube (scaled to keep label visually comparable in size)
    const float cubeSize = 0.8f;
    const float cubeHalf = cubeSize * 0.5f;
    osg::ref_ptr<osg::Box> cube = new osg::Box(osg::Vec3(0.f, 0.f, 0.f), cubeSize );
    osg::ref_ptr<osg::ShapeDrawable> cubeDrawable =
					new osg::ShapeDrawable( cube.get() );
    geode->addDrawable( cubeDrawable.get() );

    // Line
    osg::ref_ptr<osg::Geometry> line = new osg::Geometry;

    osg::ref_ptr<osg::Vec3Array> vertices = new osg::Vec3Array;
    vertices->push_back( osg::Vec3(-1.f, 0.f, 0.f) );
    vertices->push_back( osg::Vec3(1.f, 0.f, 0.f) );
    line->setVertexArray( vertices.get() );
    line->addPrimitiveSet( new osg::DrawArrays(GL_LINES, 0, 2) );

    osg::ref_ptr<osg::Vec4Array> colors = new osg::Vec4Array;
    colors->push_back( osg::Vec4(1.f, 0.f, 0.f, 1.f) ); // red
    line->setColorArray( colors.get(), osg::Array::BIND_OVERALL );

    osg::StateSet* lineState = line->getOrCreateStateSet();
    lineState->setAttribute( new osg::LineWidth(3.0f), osg::StateAttribute::ON);
    lineState->setMode( GL_LIGHTING, osg::StateAttribute::OFF );
    geode->addDrawable( line.get() );

    // Label
    osg::ref_ptr<osg::Geode> label = new osg::Geode;
    labeltext_ = new osgText::Text;
    cubecenter_.set( 0.f, 0.f, 0.f );
    labelanchor_.set( cubeHalf, cubeHalf, cubeHalf + 0.02f );
    labeltext_->setText( "Hello, OSG!" );
    labeltext_->setFont( "Arial" );
    labeltext_->setAxisAlignment( osgText::TextBase::SCREEN );
    labeltext_->setCharacterSize( 0.18f );
    labeltext_->setAlignment( osgText::TextBase::LEFT_BOTTOM );
    // Anchor label on one cube edge, with a tiny offset.
    labeltext_->setPosition( labelanchor_ );
    label->addDrawable( labeltext_.get() );

    // Final scene setup
    root_->addChild( geode.get() );
    root_->addChild( label.get() );

    viewer_->setSceneData( root_.get() );
    osg::ref_ptr<osgGA::TrackballManipulator> manip = new osgGA::TrackballManipulator();
    const double distance = 4.5;
    const double azimuthDeg = -50.0;   // Rotation around Z (yaw)
    const double elevationDeg = 25.0;  // Tilt up from XY plane
    const double degToRad = osg::PI / 180.0;
    const double azimuthRad = azimuthDeg * degToRad;
    const double elevationRad = elevationDeg * degToRad;
    const osg::Vec3d eye( cubecenter_.x() + distance * std::cos(elevationRad) * std::cos(azimuthRad),
			  cubecenter_.y() + distance * std::cos(elevationRad) * std::sin(azimuthRad),
    			  cubecenter_.z() + distance * std::sin(elevationRad) );
    manip->setHomePosition(eye, cubecenter_, osg::Vec3d(0.0, 0.0, 1.0), false);
    viewer_->setCameraManipulator( manip.get() );
    manip->home(0.0);
}


static bool matricesAreClose( const osg::Matrix& m1, const osg::Matrix& m2,
			      double epsilon = 0.0001 )
{
    for ( int i = 0; i < 16; i++ ) 
    {
	if ( std::abs(m1.ptr()[i] - m2.ptr()[i]) > epsilon )
	    return false;
    }

    return true;
}


void OSGWidget::paintGL()
{
    if ( !viewer_.valid() )
	    return;

    updateLabelAlignment();
    viewer_->frame();
    const osg::Matrix currentviewmatrix =
				viewer_->getCameraManipulator()->getMatrix();
    if ( matricesAreClose(currentviewmatrix,lastviewmatrix_) )
    {
	stillframecount_++;
	if ( stillframecount_ >= maxstillframes_ )
	    rendertimer_->stop();
    }
    else
    {
	stillframecount_ = 0;
	lastviewmatrix_ = currentviewmatrix;
	if ( !rendertimer_->isActive() )
	    rendertimer_->start( rendertimems_ );
    }
}


void OSGWidget::updateLabelAlignment()
{
    if ( !viewer_.valid() || !labeltext_.valid() )
	return;

    osg::Camera* cam = viewer_->getCamera();
    osg::Viewport* vp = cam ? cam->getViewport() : nullptr;
    if ( !cam || !vp )
	return;

    const osg::Matrix mvpw = cam->getViewMatrix() *
                             cam->getProjectionMatrix() *
                             vp->computeWindowMatrix();
    const osg::Vec3 centerWin = cubecenter_ * mvpw;
    const osg::Vec3 anchorWin = labelanchor_ * mvpw;

    labeltext_->setAlignment( anchorWin.x() >= centerWin.x()
                              ? osgText::TextBase::LEFT_BOTTOM
                              : osgText::TextBase::RIGHT_BOTTOM );
}


void OSGWidget::resizeGL( int w, int h )
{
    if ( gw_.valid() )
    {
	gw_->resized( x(), y(), w, h );
	gw_->getEventQueue()->windowResize( x(), y(), w, h );
    }

    if ( viewer_.valid() )
    {
	viewer_->getCamera()->setViewport( new osg::Viewport(0, 0, w, h) );
	viewer_->getCamera()->setProjectionMatrixAsPerspective(30.,
				static_cast<double>(w) / h, 1., 10000.);
    }
}


void OSGWidget::mousePressEvent( QMouseEvent* event )
{
    const Qt::MouseButton qtbut = event->button();
    if ( qtbut != Qt::LeftButton && qtbut != Qt::MiddleButton &&
	 qtbut != Qt::RightButton )
	return;

    const QPointF pos = event->position();
    const osgGA::GUIEventAdapter::MouseButtonMask osgbut =
						  mapQtMouseButton( qtbut );
    gw_->getEventQueue()->mouseButtonPress( pos.x(), pos.y(), osgbut );
    update();
}


void OSGWidget::mouseReleaseEvent( QMouseEvent* event )
{
    const Qt::MouseButton qtbut = event->button();
    if ( qtbut != Qt::LeftButton && qtbut != Qt::MiddleButton &&
	 qtbut != Qt::RightButton )
	return;

    const QPointF pos = event->position();
    const osgGA::GUIEventAdapter::MouseButtonMask osgbut =
						  mapQtMouseButton( qtbut );
    gw_->getEventQueue()->mouseButtonRelease( pos.x(), pos.y(), osgbut );
    if ( !rendertimer_->isActive() )
	rendertimer_->start( rendertimems_ );
}


void OSGWidget::mouseMoveEvent( QMouseEvent* event )
{
    const QPointF pos = event->position();
    gw_->getEventQueue()->mouseMotion( pos.x(), pos.y() );
    if ( !rendertimer_->isActive() )
	rendertimer_->start( rendertimems_ );
}


void OSGWidget::wheelEvent( QWheelEvent* event )
{
    const float delta = static_cast<float>(event->angleDelta().y()) / 120.0f;
    if ( delta > 0 )
	gw_->getEventQueue()->mouseScroll( osgGA::GUIEventAdapter::SCROLL_UP );
    else
	gw_->getEventQueue()->mouseScroll( osgGA::GUIEventAdapter::SCROLL_DOWN);

    update();
}


void OSGWidget::keyPressEvent( QKeyEvent* event )
{
    gw_->getEventQueue()->keyPress( mapQtKey(event) );
    update();
}


void OSGWidget::keyReleaseEvent(QKeyEvent* event)
{
    gw_->getEventQueue()->keyRelease( mapQtKey(event) );
    update();
}


osgGA::GUIEventAdapter::MouseButtonMask
    OSGWidget::mapQtMouseButton(Qt::MouseButton button)
{
    switch ( button )
    {
	case Qt::LeftButton: return osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON;
	case Qt::MiddleButton:
			     return osgGA::GUIEventAdapter::MIDDLE_MOUSE_BUTTON;
	case Qt::RightButton: return osgGA::GUIEventAdapter::RIGHT_MOUSE_BUTTON;
	default: return osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON;
    }
}


int OSGWidget::mapQtKey( QKeyEvent* event )
{
    return event->key();
}


bool OSGWidget::doScreenShot( const QSize& /*dpi*/, double scalefactor,
                              const char* destpath, std::string& msg ) const
{
    if ( !gw_.valid() || !viewer_.valid() || !root_.valid() )
    {
	msg = "Error: Viewer or scene not initialized";
	return false;
    }

    const int targetW = this->width() * scalefactor;
    const int targetH = this->height() * scalefactor;

    osg::Camera* cam = viewer_->getCamera();
    if ( !cam )
    {
	msg = "Error: No viewer camera available";
	return false;
    }

    osg::ref_ptr<osg::GraphicsContext::Traits> traits =
					new osg::GraphicsContext::Traits;
    traits->x = 0;
    traits->y = 0;
    traits->width = targetW;
    traits->height = targetH;
    traits->windowDecoration = false;
    traits->doubleBuffer = false;
    traits->pbuffer = true;
    traits->sharedContext = nullptr;

    osg::ref_ptr<osg::GraphicsContext> offGc =
		osg::GraphicsContext::createGraphicsContext( traits.get() );
    if ( !offGc.valid() )
    {
	msg = "Failed to create offscreen graphics context";
        return false;
    }

    osg::ref_ptr<osg::Image> colorImg = new osg::Image;
    colorImg->allocateImage(targetW, targetH, 1, GL_RGBA, GL_UNSIGNED_BYTE);
    osg::ref_ptr<osg::Camera> offCam = new osg::Camera;
    offCam->setGraphicsContext( offGc.get() );
    offCam->setReferenceFrame( cam->getReferenceFrame() );
    offCam->setViewMatrix( cam->getViewMatrix() );
    offCam->setProjectionMatrix( cam->getProjectionMatrix() );
    offCam->setViewport( new osg::Viewport(0, 0, targetW, targetH) );
    offCam->setCullMask( cam->getCullMask() );
    offCam->setClearMask( cam->getClearMask() );
    offCam->setClearColor( cam->getClearColor() );
    offCam->setClearDepth( cam->getClearDepth() );
    offCam->setClearStencil( cam->getClearStencil() );
    offCam->setComputeNearFarMode( cam->getComputeNearFarMode() );
    offCam->setRenderOrder( osg::Camera::NESTED_RENDER );
    offCam->setRenderOrder( cam->getRenderOrder(), cam->getRenderOrderNum() );
    offCam->setRenderTargetImplementation( osg::Camera::FRAME_BUFFER_OBJECT );
    offCam->attach( osg::Camera::COLOR_BUFFER0, colorImg.get() );
    offCam->attach( osg::Camera::DEPTH_BUFFER, GL_DEPTH_COMPONENT24 );
    offCam->setDrawBuffer( GL_COLOR_ATTACHMENT0 );
    offCam->setReadBuffer( GL_COLOR_ATTACHMENT0 );
    if ( cam->getStateSet() )
        offCam->setStateSet( cam->getStateSet() );

    // Keep depth testing explicit and avoid face culling artifacts in offscreen path.
    offCam->getOrCreateStateSet()->setAttributeAndModes(new osg::Depth,
						osg::StateAttribute::ON );
    offCam->getOrCreateStateSet()->setMode( GL_CULL_FACE,
					    osg::StateAttribute::OFF );
    osg::ref_ptr<osg::Node> offScene =
	dynamic_cast<osg::Node*>(root_->clone( osg::CopyOp::DEEP_COPY_ALL) );
    if ( !offScene.valid() )
    {
        msg = "Failed to clone scene for offscreen rendering";
        return false;
    }

    osgViewer::Viewer offViewer;
    offViewer.setThreadingModel( osgViewer::Viewer::SingleThreaded );
    offViewer.setLightingMode( viewer_->getLightingMode() );
    offViewer.setCamera( offCam.get() );
    offViewer.setSceneData( offScene.get() );
    offViewer.realize();
    offViewer.frame();
    offViewer.frame();
    offGc->releaseContext();

    QImage img;
    if ( colorImg.valid() && colorImg->data() )
    {
        img = QImage( colorImg->data(), targetW, targetH,
		      QImage::Format_RGBA8888).flipped( Qt::Vertical );
    }

    const QString outpath( destpath );
    if ( !img.save(outpath) )
    {
        msg = "Failed to save screenshot to " + outpath.toStdString();
	return false;
    }

    msg = "Screenshot saved to '" + outpath.toStdString() + "' (" +
           std::to_string(img.width()) + "x" + std::to_string(img.height()) + ")";

    return true;
}


// main app

int main( int argc, char** argv )
{
    // Set up compatibility OpenGL profile
    QSurfaceFormat fmt;
    fmt.setVersion( 2, 0 );
    fmt.setProfile( QSurfaceFormat::CompatibilityProfile );
    fmt.setDepthBufferSize( 24 );
    QSurfaceFormat::setDefaultFormat( fmt );

    QApplication app( argc, argv );

    OSGWindow window;
    window.setWindowTitle( "Minimal Qt6 + OSG 3.6.5 Embedded Example" );

    auto* mdiarea = new QMdiArea;
    window.setCentralWidget( mdiarea );

    auto* osgwidget = new OSGWidget;
    auto* container = new QWidget;

    auto* layout = new QVBoxLayout( container );
    layout->setContentsMargins( 0, 0, 0, 0 );
    layout->addWidget(osgwidget);

    container->setLayout( layout );

    auto* subwindow = new QMdiSubWindow;
    subwindow->setWindowTitle( "3D View" );
    subwindow->setWidget( container );
    subwindow->setAttribute( Qt::WA_DeleteOnClose );
    mdiarea->addSubWindow( subwindow );
    subwindow->showMaximized();

    window.setOSGWidget( osgwidget );
    window.resize( 800, 600 );
    window.show();

    return app.exec();
}
