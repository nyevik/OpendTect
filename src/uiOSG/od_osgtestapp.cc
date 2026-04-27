#include <QAction>
#include <QApplication>
#include <QFileDialog>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
#include <osg/Hint>
#include <osg/LineWidth>
#include <osg/Image>
#include <osg/Texture2D>
#include <osg/FrameBufferObject>
#include <osg/GraphicsContext>
#include <osg/Matrix>
#include <osg/Depth>
#include <osg/ShapeDrawable>
#include <osg/CopyOp>
#include <osgDB/ReadFile>
#include <osgDB/WriteFile>
#include <osgGA/TrackballManipulator>
#include <osgText/Text>
#include <osgViewer/Viewer>

#include <iostream>

#define OFFSCREEN_MSAA_SAMPLES 4

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

    bool loadScene(const char* srcpath,std::string& msg);
    bool saveScene(const char* destpath,std::string& msg) const;
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
    osg::Geometry* addCube(osg::Geode&,float& cubesize);
    osg::Geometry* addLine(osg::Geode&);
    osgText::Text* addLabel(osg::Geode&,float cubesize);

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
    connect( screenshotact, &QAction::triggered, this,
	     &OSGWindow::onTakeScreenshot );
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
    if ( !osgwidget_ )
    {
        QMessageBox::critical( this, "Error", "No viewer available" );
        return;
    }

    const QString path = QFileDialog::getOpenFileName(
        this, "Open Scene", QString(), "OSG Scene (*.osgb *.osgt)" );
    if ( path.isEmpty() )
        return;

    std::string msg;
    if ( !osgwidget_->loadScene(path.toStdString().c_str(),msg) )
    {
        QMessageBox::critical( this, "Error", msg.c_str() );
        return;
    }

    QMessageBox::information( this, "Open Scene", msg.c_str() );
}


void OSGWindow::onSave()
{
    if ( !osgwidget_ )
    {
        QMessageBox::critical( this, "Error", "No viewer available" );
        return;
    }

    const QString path = QFileDialog::getSaveFileName(
        this, "Save Scene", QString(), "OSG Scene (*.osgb *.osgt)" );
    if ( path.isEmpty() )
        return;

    std::string msg;
    if ( !osgwidget_->saveScene(path.toStdString().c_str(),msg) )
    {
        QMessageBox::critical( this, "Error", msg.c_str() );
        return;
    }

    QMessageBox::information( this, "Save Scene", msg.c_str() );
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
    if ( !osgwidget_->doScreenShot(currentScreenDPI(),scalefactor,
				   outpath.toStdString().c_str(),msg) )
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


bool OSGWidget::saveScene( const char* destpath, std::string& msg ) const
{
    if ( !root_.valid() )
    {
        msg = "Error: Scene is not initialized";
        return false;
    }

    if ( !destpath || !*destpath )
    {
        msg = "Error: Invalid destination path";
        return false;
    }

    if ( !osgDB::writeNodeFile(*root_,destpath) )
    {
        msg = std::string("Failed to save scene to ") + destpath;
        return false;
    }

    QString infomsg = QString("Scene saved to %1").arg(destpath);
    if ( viewer_.valid() && viewer_->getCamera() )
    {
        const osg::Matrixd vm = viewer_->getCamera()->getViewMatrix();
        QJsonArray mtx;
        for ( int i=0; i<16; i++ )
            mtx.append( vm.ptr()[i] );

        QJsonObject obj;
        obj["version"] = 1;
        obj["view_matrix"] = mtx;

        const QString camPath = QString(destpath) + ".camera.json";
        QFile camFile( camPath );
        if ( camFile.open(QIODevice::WriteOnly|QIODevice::Truncate) )
        {
            camFile.write( QJsonDocument(obj).toJson(QJsonDocument::Indented) );
            camFile.close();
        }
        else
        {
            infomsg += QString("\nWarning: failed to write camera state: %1")
                       .arg(camPath);
        }
    }

    msg = infomsg.toStdString();
    return true;
}


bool OSGWidget::loadScene( const char* srcpath, std::string& msg )
{
    if ( !viewer_.valid() )
    {
        msg = "Error: Viewer is not initialized";
        return false;
    }

    if ( !srcpath || !*srcpath )
    {
        msg = "Error: Invalid source path";
        return false;
    }

    osg::ref_ptr<osg::Node> loaded = osgDB::readNodeFile(srcpath);
    if ( !loaded.valid() )
    {
        msg = std::string("Failed to load scene from ") + srcpath;
        return false;
    }

    osg::ref_ptr<osg::Group> newRoot = new osg::Group;
    newRoot->addChild( loaded.get() );
    root_ = newRoot.get();
    viewer_->setSceneData( root_.get() );

    labeltext_ = nullptr;
    cubecenter_.set( 0.f, 0.f, 0.f );
    labelanchor_.set( 0.f, 0.f, 0.f );

    if ( !rendertimer_->isActive() )
        rendertimer_->start( rendertimems_ );

    update();

    QString infomsg = QString("Scene loaded from %1").arg(srcpath);
    const QString camPath = QString(srcpath) + ".camera.json";
    QFile camFile( camPath );
    if ( camFile.open(QIODevice::ReadOnly) )
    {
        const QJsonDocument doc = QJsonDocument::fromJson( camFile.readAll() );
        camFile.close();
        if ( doc.isObject() )
        {
            const QJsonArray mtx = doc.object().value("view_matrix").toArray();
            if ( mtx.size() == 16 )
            {
                osg::Matrixd vm;
                for ( int i=0; i<16; i++ )
                    vm.ptr()[i] = mtx.at(i).toDouble();

                if ( auto* manip = viewer_->getCameraManipulator() )
                    manip->setByInverseMatrix( vm );
                else if ( viewer_->getCamera() )
                    viewer_->getCamera()->setViewMatrix( vm );
            }
            else
            {
                infomsg += "\nWarning: invalid camera sidecar format";
            }
        }
        else
        {
            infomsg += "\nWarning: invalid camera sidecar JSON";
        }
    }

    msg = infomsg.toStdString();
    return true;
}


osg::Geometry* OSGWidget::addCube( osg::Geode& geode, float& cubeSize )
{
    // Cube (scaled to keep label visually comparable in size)
    cubeSize = 0.8f;
    cubecenter_.set( 0.f, 0.f, 0.f );
    const float cubeHalf = cubeSize * 0.5f;
    osg::ref_ptr<osg::Geometry> cubeGeom = new osg::Geometry;
    osg::ref_ptr<osg::Vec3Array> cubeVerts = new osg::Vec3Array;
    osg::ref_ptr<osg::Vec2Array> cubeUVs = new osg::Vec2Array;
    osg::ref_ptr<osg::Vec3Array> cubeNormals = new osg::Vec3Array;

    const osg::Vec3 p000( cubecenter_.x()-cubeHalf, cubecenter_.y()-cubeHalf,
			  cubecenter_.z()-cubeHalf );
    const osg::Vec3 p001( cubecenter_.x()-cubeHalf, cubecenter_.y()-cubeHalf,
			  cubecenter_.z()+cubeHalf );
    const osg::Vec3 p010( cubecenter_.x()-cubeHalf, cubecenter_.y()+cubeHalf,
			  cubecenter_.z()-cubeHalf );
    const osg::Vec3 p011( cubecenter_.x()-cubeHalf, cubecenter_.y()+cubeHalf,
			  cubecenter_.z()+cubeHalf );
    const osg::Vec3 p100( cubecenter_.x()+cubeHalf, cubecenter_.y()-cubeHalf,
			  cubecenter_.z()-cubeHalf );
    const osg::Vec3 p101( cubecenter_.x()+cubeHalf, cubecenter_.y()-cubeHalf,
			  cubecenter_.z()+cubeHalf );
    const osg::Vec3 p110( cubecenter_.x()+cubeHalf, cubecenter_.y()+cubeHalf,
			  cubecenter_.z()-cubeHalf );
    const osg::Vec3 p111( cubecenter_.x()+cubeHalf, cubecenter_.y()+cubeHalf,
			  cubecenter_.z()+cubeHalf );

    auto addFace = [&]( const osg::Vec3& a, const osg::Vec3& b,
			const osg::Vec3& c, const osg::Vec3& d,
			const osg::Vec3& nrm )
    {
	cubeVerts->push_back( a ); cubeUVs->push_back( osg::Vec2(0.f,0.f) );
	cubeVerts->push_back( b ); cubeUVs->push_back( osg::Vec2(1.f,0.f) );
	cubeVerts->push_back( c ); cubeUVs->push_back( osg::Vec2(1.f,1.f) );
	cubeVerts->push_back( d ); cubeUVs->push_back( osg::Vec2(0.f,1.f) );
	cubeNormals->push_back( nrm );
    };

    addFace( p100, p101, p111, p110, osg::Vec3( 1.f, 0.f, 0.f) ); // +X
    addFace( p000, p010, p011, p001, osg::Vec3(-1.f, 0.f, 0.f) ); // -X
    addFace( p010, p110, p111, p011, osg::Vec3( 0.f, 1.f, 0.f) ); // +Y
    addFace( p000, p001, p101, p100, osg::Vec3( 0.f,-1.f, 0.f) ); // -Y
    addFace( p001, p011, p111, p101, osg::Vec3( 0.f, 0.f, 1.f) ); // +Z
    addFace( p000, p100, p110, p010, osg::Vec3( 0.f, 0.f,-1.f) ); // -Z

    cubeGeom->setVertexArray( cubeVerts.get() );
    cubeGeom->setTexCoordArray( 0, cubeUVs.get() );
    cubeGeom->setNormalArray( cubeNormals.get(),
			      osg::Array::BIND_PER_PRIMITIVE_SET );
    for ( unsigned int faceidx=0; faceidx<6; faceidx++ )
	cubeGeom->addPrimitiveSet( new osg::DrawArrays(GL_QUADS, faceidx*4, 4));

    osg::ref_ptr<osg::Image> checkerimg = new osg::Image;
    const int texw = 128;
    const int texh = 128;
    checkerimg->allocateImage( texw, texh, 1, GL_RGBA, GL_UNSIGNED_BYTE );
    for ( int y=0; y<texh; y++ )
    {
	for ( int x=0; x<texw; x++ )
	{
	    const bool dark = ( (x/16 + y/16) % 2 ) == 0;
	    const unsigned char r = dark ? 30 : 200;
	    const unsigned char g = dark ? 80 : 220;
	    const unsigned char b = dark ? 180 : 40;
	    unsigned char* px = checkerimg->data( x, y );
	    px[0] = r; px[1] = g; px[2] = b; px[3] = 255;
	}
    }

    osg::ref_ptr<osg::Texture2D> cubetex = new osg::Texture2D(checkerimg.get());
    cubetex->setFilter( osg::Texture::MIN_FILTER,
			osg::Texture::LINEAR_MIPMAP_LINEAR );
    cubetex->setFilter( osg::Texture::MAG_FILTER, osg::Texture::LINEAR );
    cubetex->setWrap( osg::Texture::WRAP_S, osg::Texture::REPEAT );
    cubetex->setWrap( osg::Texture::WRAP_T, osg::Texture::REPEAT );
    cubetex->setResizeNonPowerOfTwoHint( false );

    osg::StateSet* cubestate = cubeGeom->getOrCreateStateSet();
    cubestate->setTextureAttributeAndModes( 0, cubetex.get(),
					    osg::StateAttribute::ON );
    cubestate->setMode( GL_LIGHTING, osg::StateAttribute::ON );

    geode.addDrawable( cubeGeom.get() );
    return cubeGeom.get();
}


osg::Geometry* OSGWidget::addLine( osg::Geode& geode )
{
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

    geode.addDrawable( line.get() );
    return line.get();
}


osgText::Text* OSGWidget::addLabel( osg::Geode& geode, float cubesize )
{
    const float cubehalf = cubesize * 0.5f;

    osg::ref_ptr<osgText::Text> label = new osgText::Text;
    labelanchor_.set( cubecenter_.x()+cubehalf, cubecenter_.y()+cubehalf,
		      cubecenter_.z()+cubehalf+0.02f );
    label->setText( "Hello, OSG!" );
    label->setFont( "Arial" );
    label->setAxisAlignment( osgText::TextBase::SCREEN );
    label->setCharacterSize( 0.18f );
    label->setAlignment( osgText::TextBase::LEFT_BOTTOM );
    // Anchor label on one cube edge, with a tiny offset.
    label->setPosition( labelanchor_ );
    geode.addDrawable( label.get() );
    return label.get();
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
    traits->sampleBuffers = 1;
    traits->samples = OFFSCREEN_MSAA_SAMPLES;

    gw_ = new osgViewer::GraphicsWindowEmbedded( traits->x, traits->y,
						 traits->width, traits->height);

    osg::ref_ptr<osg::Camera> camera = viewer_->getCamera();
    camera->setGraphicsContext( gw_.get() );
    camera->setViewport( new osg::Viewport(0, 0, traits->width,traits->height));
    camera->setProjectionMatrixAsPerspective( 30., double(width()) / height(),
					      1., 10000.);
    camera->setClearColor( osg::Vec4(0.2f, 0.2f, 0.2f, 1.f) );
			   // Nice neutral gray

    // Build a scene with a cube, a line and a label
    root_ = new osg::Group;
    osg::ref_ptr<osg::Geode> geode = new osg::Geode;

    float cubesize;
    addCube( *geode, cubesize );
    addLine( *geode );
    auto* label = addLabel( *geode, cubesize );

    // Final scene setup
    root_->addChild( geode.get() );
    root_->addChild( label );

    viewer_->setSceneData( root_.get() );
    osg::ref_ptr<osgGA::TrackballManipulator> manip =
					new osgGA::TrackballManipulator();
    const double distance = 4.5;
    const double azimuthDeg = -50.0;   // Rotation around Z (yaw)
    const double elevationDeg = 25.0;  // Tilt up from XY plane
    const double degToRad = osg::PI / 180.0;
    const double azimuthRad = azimuthDeg * degToRad;
    const double elevationRad = elevationDeg * degToRad;
    const osg::Vec3d eye(
	cubecenter_.x() +
		distance * std::cos(elevationRad) * std::cos(azimuthRad),
	cubecenter_.y() +
		distance * std::cos(elevationRad) * std::sin(azimuthRad),
	cubecenter_.z()
		+ distance * std::sin(elevationRad) );
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
    traits->sampleBuffers = 1;
    traits->samples = OFFSCREEN_MSAA_SAMPLES;

    osg::ref_ptr<osg::GraphicsContext> offGc =
		osg::GraphicsContext::createGraphicsContext( traits.get() );
    if ( !offGc.valid() )
    {
	msg = "Failed to create offscreen graphics context";
        return false;
    }

    osg::ref_ptr<osg::Image> colorImg = new osg::Image;
    colorImg->allocateImage( targetW, targetH, 1, GL_RGBA, GL_UNSIGNED_BYTE );
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

    offCam->getOrCreateStateSet()->setAttributeAndModes(new osg::Depth,
						osg::StateAttribute::ON );
    offCam->getOrCreateStateSet()->setMode( GL_CULL_FACE,
					    osg::StateAttribute::OFF );
    offCam->getOrCreateStateSet()->setMode( GL_MULTISAMPLE,
					    osg::StateAttribute::ON );
    offCam->getOrCreateStateSet()->setMode( GL_LINE_SMOOTH,
					    osg::StateAttribute::ON );
    offCam->getOrCreateStateSet()->setMode( GL_POINT_SMOOTH,
					    osg::StateAttribute::ON );
    offCam->getOrCreateStateSet()->setAttributeAndModes(
			    new osg::Hint( GL_LINE_SMOOTH_HINT, GL_NICEST ),
			    osg::StateAttribute::ON );
    offCam->getOrCreateStateSet()->setAttributeAndModes(
			    new osg::Hint( GL_POINT_SMOOTH_HINT, GL_NICEST ),
			    osg::StateAttribute::ON );

    osgViewer::Viewer offViewer;
    offViewer.setThreadingModel( osgViewer::Viewer::SingleThreaded );
    offViewer.setLightingMode( viewer_->getLightingMode() );
    offViewer.setCamera( offCam.get() );
    offViewer.setSceneData( viewer_->getSceneData() );
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

    msg = "Screenshot saved to '" + outpath.toStdString()
		+ "' (" + std::to_string(img.width()) + "x"
		+ std::to_string(img.height()) + ")";

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
