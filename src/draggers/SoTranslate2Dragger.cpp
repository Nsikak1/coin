/**************************************************************************\
 *
 *  Copyright (C) 1998-2000 by Systems in Motion.  All rights reserved.
 *
 *  This file is part of the Coin library.
 *
 *  This file may be distributed under the terms of the Q Public License
 *  as defined by Troll Tech AS of Norway and appearing in the file
 *  LICENSE.QPL included in the packaging of this file.
 *
 *  If you want to use Coin in applications not covered by licenses
 *  compatible with the QPL, you can contact SIM to aquire a
 *  Professional Edition license for Coin.
 *
 *  Systems in Motion AS, Prof. Brochs gate 6, N-7030 Trondheim, NORWAY
 *  http://www.sim.no/ sales@sim.no Voice: +47 22114160 Fax: +47 67172912
 *
\**************************************************************************/

#include <Inventor/draggers/SoTranslate2Dragger.h>
#include <Inventor/nodekits/SoSubKitP.h>
#include <Inventor/nodes/SoSeparator.h>
#include <Inventor/nodes/SoSwitch.h>
#include <Inventor/projectors/SbPlaneProjector.h>
#include <Inventor/sensors/SoFieldSensor.h>
#include <Inventor/events/SoKeyboardEvent.h>

#include <data/draggerDefaults/translate2Dragger.h>

#define CONSTRAINT_OFF  0
#define CONSTRAINT_WAIT 1
#define CONSTRAINT_X    2
#define CONSTRAINT_Y    3

SO_KIT_SOURCE(SoTranslate2Dragger);

void
SoTranslate2Dragger::initClass(void)
{
  SO_KIT_INTERNAL_INIT_CLASS(SoTranslate2Dragger);
}

SoTranslate2Dragger::SoTranslate2Dragger(void)
{
  SO_KIT_INTERNAL_CONSTRUCTOR(SoTranslate2Dragger);

  SO_KIT_ADD_CATALOG_ENTRY(translatorSwitch, SoSwitch, TRUE, geomSeparator, feedbackSwitch, FALSE);
  SO_KIT_ADD_CATALOG_ENTRY(translator, SoSeparator, TRUE, translatorSwitch, translatorActive, TRUE);
  SO_KIT_ADD_CATALOG_ENTRY(translatorActive, SoSeparator, TRUE, translatorSwitch, "", TRUE);
  SO_KIT_ADD_CATALOG_ENTRY(feedbackSwitch, SoSwitch, TRUE, geomSeparator, axisFeedbackSwitch, FALSE);
  SO_KIT_ADD_CATALOG_ENTRY(feedback, SoSeparator, TRUE, feedbackSwitch, feedbackActive, TRUE);
  SO_KIT_ADD_CATALOG_ENTRY(feedbackActive, SoSeparator, TRUE, feedbackSwitch, "", TRUE);
  SO_KIT_ADD_CATALOG_ENTRY(axisFeedbackSwitch, SoSwitch, TRUE, geomSeparator, "", FALSE);
  SO_KIT_ADD_CATALOG_ENTRY(xAxisFeedback, SoSeparator, TRUE, axisFeedbackSwitch, yAxisFeedback, TRUE);
  SO_KIT_ADD_CATALOG_ENTRY(yAxisFeedback, SoSeparator, TRUE, axisFeedbackSwitch, "", TRUE);

  if (SO_KIT_IS_FIRST_INSTANCE()) {
    SoInteractionKit::readDefaultParts("translate2Dragger.iv",
                                       TRANSLATE2DRAGGER_draggergeometry,
                                       sizeof(TRANSLATE2DRAGGER_draggergeometry));
  }
  SO_NODE_ADD_FIELD(translation, (0.0f, 0.0f, 0.0f));
  SO_KIT_INIT_INSTANCE();

  // initialize default parts
  this->setPartAsDefault("translator", "translate2Translator");
  this->setPartAsDefault("translatorActive", "translate2TranslatorActive");
  this->setPartAsDefault("feedback", "translate2Feedback");
  this->setPartAsDefault("feedbackActive", "translate2FeedbackActive");
  this->setPartAsDefault("xAxisFeedback", "translate2XAxisFeedback");
  this->setPartAsDefault("yAxisFeedback", "translate2YAxisFeedback");

  // initialize swich values
  SoSwitch *sw;
  sw = SO_GET_ANY_PART(this, "translatorSwitch", SoSwitch);
  SoInteractionKit::setSwitchValue(sw, 0);
  sw = SO_GET_ANY_PART(this, "feedbackSwitch", SoSwitch);
  SoInteractionKit::setSwitchValue(sw, 0);
  sw = SO_GET_ANY_PART(this, "axisFeedbackSwitch", SoSwitch);
  SoInteractionKit::setSwitchValue(sw, SO_SWITCH_NONE);

  // setup projector
  this->planeProj = new SbPlaneProjector();
  this->addStartCallback(SoTranslate2Dragger::startCB);
  this->addMotionCallback(SoTranslate2Dragger::motionCB);
  this->addFinishCallback(SoTranslate2Dragger::finishCB);
  this->addOtherEventCallback(SoTranslate2Dragger::metaKeyChangeCB);
  this->addValueChangedCallback(SoTranslate2Dragger::valueChangedCB);

  this->fieldSensor = new SoFieldSensor(SoTranslate2Dragger::fieldSensorCB, this);
  this->fieldSensor->setPriority(0);

  this->constraintState = CONSTRAINT_OFF;

  this->setUpConnections(TRUE, TRUE);
}


SoTranslate2Dragger::~SoTranslate2Dragger()
{
  delete this->planeProj;
  delete this->fieldSensor;
}

SbBool
SoTranslate2Dragger::setUpConnections(SbBool onoff, SbBool doitalways)
{
  if (!doitalways && this->connectionsSetUp == onoff) return onoff;

  SbBool oldval = this->connectionsSetUp;

  if (onoff) {
    inherited::setUpConnections(onoff, doitalways);

    SoTranslate2Dragger::fieldSensorCB(this, NULL);

    if (this->fieldSensor->getAttachedField() != &this->translation) {
      this->fieldSensor->attach(&this->translation);
    }
  }
  else {
    if (this->fieldSensor->getAttachedField() != NULL) {
      this->fieldSensor->detach();
    }
    inherited::setUpConnections(onoff, doitalways);
  }
  this->connectionsSetUp = onoff;
  return oldval;
}

void
SoTranslate2Dragger::fieldSensorCB(void * d, SoSensor * s)
{
  assert(d);
  SoTranslate2Dragger *thisp = (SoTranslate2Dragger*)d;
  SbMatrix matrix = thisp->getMotionMatrix();
  SbVec3f t = thisp->translation.getValue();
  matrix[3][0] = t[0];
  matrix[3][1] = t[1];
  matrix[3][2] = t[2];
  thisp->setMotionMatrix(matrix);
}

void
SoTranslate2Dragger::valueChangedCB(void *, SoDragger * d)
{
  SoTranslate2Dragger *thisp = (SoTranslate2Dragger*)d;
  SbMatrix matrix = thisp->getMotionMatrix();

  SbVec3f t;
  t[0] = matrix[3][0];
  t[1] = matrix[3][1];
  t[2] = matrix[3][2];

  thisp->fieldSensor->detach();
  if (thisp->translation.getValue() != t) {
    thisp->translation = t;
  }
  thisp->fieldSensor->attach(&thisp->translation);
}

void
SoTranslate2Dragger::startCB(void *, SoDragger * d)
{
  SoTranslate2Dragger *thisp = (SoTranslate2Dragger*)d;
  thisp->dragStart();
}

void
SoTranslate2Dragger::motionCB(void *, SoDragger * d)
{
  SoTranslate2Dragger *thisp = (SoTranslate2Dragger*)d;
  thisp->drag();
}

void
SoTranslate2Dragger::finishCB(void *, SoDragger * d)
{
  SoTranslate2Dragger *thisp = (SoTranslate2Dragger*)d;
  thisp->dragFinish();
}

void
SoTranslate2Dragger::metaKeyChangeCB(void *, SoDragger *d)
{
  SoTranslate2Dragger *thisp = (SoTranslate2Dragger*)d;
  if (!thisp->isActive.getValue()) return;

  const SoEvent *event = thisp->getEvent();
  if (thisp->constraintState == CONSTRAINT_OFF &&
      event->wasShiftDown()) thisp->drag();
  else if (thisp->constraintState != CONSTRAINT_OFF &&
           !event->wasShiftDown()) thisp->drag();
}

void
SoTranslate2Dragger::dragStart(void)
{
  SoSwitch *sw;
  sw = SO_GET_ANY_PART(this, "translatorSwitch", SoSwitch);
  SoInteractionKit::setSwitchValue(sw, 1);
  sw = SO_GET_ANY_PART(this, "feedbackSwitch", SoSwitch);
  SoInteractionKit::setSwitchValue(sw, 1);
  sw = SO_GET_ANY_PART(this, "axisFeedbackSwitch", SoSwitch);
  SoInteractionKit::setSwitchValue(sw, SO_SWITCH_ALL);

  SbVec3f hitPt = this->getLocalStartingPoint();
  this->planeProj->setPlane(SbPlane(SbVec3f(0.0f, 0.0f, 1.0f), hitPt));
  if (this->getEvent()->wasShiftDown()) {
    this->getLocalToWorldMatrix().multVecMatrix(hitPt, this->worldRestartPt);
    this->constraintState = CONSTRAINT_WAIT;
  }
}

void
SoTranslate2Dragger::drag(void)
{
  this->planeProj->setViewVolume(this->getViewVolume());
  this->planeProj->setWorkingSpace(this->getLocalToWorldMatrix());

  SbVec3f projPt = this->planeProj->project(this->getNormalizedLocaterPosition());

  const SoEvent *event = this->getEvent();
  if (event->wasShiftDown() && this->constraintState == CONSTRAINT_OFF) {
    this->constraintState = CONSTRAINT_WAIT;
    this->setStartLocaterPosition(event->getPosition());
    this->getLocalToWorldMatrix().multVecMatrix(projPt, this->worldRestartPt);
  }
  else if (!event->wasShiftDown() && this->constraintState != CONSTRAINT_OFF) {
    SoSwitch *sw = SO_GET_ANY_PART(this, "axisFeedbackSwitch", SoSwitch);
    SoInteractionKit::setSwitchValue(sw, SO_SWITCH_ALL);
    this->constraintState = CONSTRAINT_OFF;
  }

  SbVec3f startPt = this->getLocalStartingPoint();
  SbVec3f motion;
  SbVec3f localrestartpt;

  if (this->constraintState != CONSTRAINT_OFF) {
    this->getWorldToLocalMatrix().multVecMatrix(this->worldRestartPt,
                                                localrestartpt);
    motion = localrestartpt - startPt;
  }
  else motion = projPt - startPt;

  switch(this->constraintState) {
  case CONSTRAINT_OFF:
    break;
  case CONSTRAINT_WAIT:
    if (this->isAdequateConstraintMotion()) {
      SbVec3f newmotion = projPt - localrestartpt;
      if (fabs(newmotion[0]) >= fabs(newmotion[1])) {
        this->constraintState = CONSTRAINT_X;
        motion[0] += newmotion[0];
        SoSwitch *sw = SO_GET_ANY_PART(this, "axisFeedbackSwitch", SoSwitch);
        SoInteractionKit::setSwitchValue(sw, 0);
      }
      else {
        this->constraintState = CONSTRAINT_Y;
        motion[1] += newmotion[1];
        SoSwitch *sw = SO_GET_ANY_PART(this, "axisFeedbackSwitch", SoSwitch);
        SoInteractionKit::setSwitchValue(sw, 1);
      }
    }
    else {
      return;
    }
    break;
  case CONSTRAINT_X:
    motion[0] += projPt[0] - localrestartpt[0];
    break;
  case CONSTRAINT_Y:
    motion[1] += projPt[1] - localrestartpt[1];
    break;
  }
  this->setMotionMatrix(this->appendTranslation(this->getStartMotionMatrix(), motion));
}

void
SoTranslate2Dragger::dragFinish(void)
{
  SoSwitch *sw;
  sw = SO_GET_ANY_PART(this, "translatorSwitch", SoSwitch);
  SoInteractionKit::setSwitchValue(sw, 0);
  sw = SO_GET_ANY_PART(this, "feedbackSwitch", SoSwitch);
  SoInteractionKit::setSwitchValue(sw, 0);
  sw = SO_GET_ANY_PART(this, "axisFeedbackSwitch", SoSwitch);
  SoInteractionKit::setSwitchValue(sw, SO_SWITCH_NONE);
  this->constraintState = CONSTRAINT_OFF;
}
