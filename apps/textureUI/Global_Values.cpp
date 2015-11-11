//----------------------------------------------------------------------------------------------------------------------------------------
// Global_Values.cpp file for Global_Values Class
// Purpose: Implements Global_Values Class
// Developer: Michael Royal - mgro224@g.uky.edu
// October 12, 2015 - Spring Semester 2016
// Last Updated 10/23/2015 by: Michael Royal
//----------------------------------------------------------------------------------------------------------------------------------------

#include "Global_Values.h"

Global_Values::Global_Values(QRect rec)
{
    // GETS THE MONITOR'S SCREEN Length & Width
    height = rec.height();
    width = rec.width();

}// End of Default Constructor()

int Global_Values::getHeight()
{
    return height;
}

int Global_Values::getWidth()
{
    return width;
}

VolumePkg * Global_Values::getVolPkg()
{
    return vpkg;
}

void Global_Values::setPath(QString newPath)
{
    path = newPath;
}

void Global_Values::createVolumePackage()
{
    vpkg = new VolumePkg(path.toStdString());// Creates a Volume Package
    VPKG_Instantiated = true;
}

void Global_Values::getMySegmentations()
{
    segmentations = vpkg->getSegmentations();
}

std::vector<std::string> Global_Values::getSegmentations()
{
    return segmentations;
}

void Global_Values::setQPixMapImage(QImage image)
{
    pix = QPixmap::fromImage(image);
}

QPixmap Global_Values::getQPixMapImage()
{
    return pix;
}

bool Global_Values::isVPKG_Intantiated()
{
    return VPKG_Instantiated;
}

void Global_Values::setWindow(QMainWindow *window)
{
    _window = window;
}

QMainWindow *Global_Values::getWindow()
{
    return _window;
}

void Global_Values::setTexture(volcart::Texture texture)
{
    _texture = texture;
}

volcart::Texture Global_Values::getTexture()
{
    return _texture;
}

void Global_Values::setRadius(double radius)
{
    _radius = radius;
}

double Global_Values::getRadius()
{
    return _radius;
}

void Global_Values::setTextureMethod(int textureMethod)
{
    _textureMethod = textureMethod;
}

int Global_Values::getTextureMethod()
{
    return _textureMethod;
}

void Global_Values::setSampleDirection(int sampleDirection)
{
    _sampleDirection = sampleDirection;
}

int Global_Values::getSampleDirection()
{
    return _sampleDirection;
}