/***************************************************************************
 *   Copyright (c) 2020 FreeCAD Developers                                 *
 *   Author: Uwe Stöhr <uwestoehr@lyx.org>                                 *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#include "PreCompiled.h"
#ifndef _PreComp_
# include <cmath>
#endif // #ifndef _PreComp_

#include <App/Document.h>
#include <App/DocumentObject.h>
#include <Base/Console.h>
#include <Base/Vector3D.h>
#include <Gui/Application.h>
#include <Gui/BitmapFactory.h>
#include <Gui/Command.h>
#include <Gui/Document.h>
#include <Gui/ViewProvider.h>
#include <Mod/TechDraw/App/DrawHatch.h>
#include <Mod/TechDraw/App/DrawUtil.h>
#include <Mod/TechDraw/App/DrawViewPart.h>

#include "TaskHatch.h"
#include "ui_TaskHatch.h"
#include "ViewProviderHatch.h"


using namespace Gui;
using namespace TechDraw;
using namespace TechDrawGui;
using DU = DrawUtil;

//ctor for creation
TaskHatch::TaskHatch(TechDraw::DrawViewPart* inDvp, std::vector<std::string> subs) :
    ui(new Ui_TaskHatch),
    m_hatch(nullptr),
    m_dvp(inDvp),
    m_subs(subs)
{
    ui->setupUi(this);

    connect(ui->fcFile, &FileChooser::fileNameSelected, this, &TaskHatch::onFileChanged);
    connect(ui->sbScale, qOverload<double>(&QuantitySpinBox::valueChanged), this, &TaskHatch::onScaleChanged);
    connect(ui->ccColor, &ColorButton::changed, this, &TaskHatch::onColorChanged);
    connect(ui->dsbRotation, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &TaskHatch::onRotationChanged);
    connect(ui->dsbOffsetX, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &TaskHatch::onOffsetChanged);
    connect(ui->dsbOffsetY, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &TaskHatch::onOffsetChanged);
    setUiPrimary();
}

//ctor for edit
TaskHatch::TaskHatch(TechDrawGui::ViewProviderHatch* inVp) :
    ui(new Ui_TaskHatch),
    m_vp(inVp)
{
//    Base::Console().message("TH::TH() - edit\n");
    ui->setupUi(this);
    m_hatch = m_vp->getViewObject();
    App::DocumentObject* obj = m_hatch->Source.getValue();
    m_dvp = static_cast<TechDraw::DrawViewPart*>(obj);

    connect(ui->fcFile, &FileChooser::fileNameSelected, this, &TaskHatch::onFileChanged);
    connect(ui->sbScale, qOverload<double>(&QuantitySpinBox::valueChanged), this, &TaskHatch::onScaleChanged);
    connect(ui->ccColor, &ColorButton::changed, this, &TaskHatch::onColorChanged);
    connect(ui->dsbRotation, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &TaskHatch::onRotationChanged);
    connect(ui->dsbOffsetX, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &TaskHatch::onOffsetChanged);
    connect(ui->dsbOffsetY, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &TaskHatch::onOffsetChanged);

    saveHatchState();
    setUiEdit();
}
TaskHatch::~TaskHatch()
{
}

void TaskHatch::setUiPrimary()
{
    setWindowTitle(QObject::tr("Create Face Hatch"));
    ui->fcFile->setFileName(QString::fromStdString(DrawHatch::prefSvgHatch()));
    ui->fcFile->setFilter(QStringLiteral(
            "SVG files (*.svg *.SVG);;Bitmap files(*.jpg *.jpeg *.png *.bmp);;All files (*)"));
    ui->sbScale->setValue(1.0);
    ui->sbScale->setSingleStep(0.1);
    ui->ccColor->setColor(TechDraw::DrawHatch::prefSvgHatchColor().asValue<QColor>());
    ui->dsbRotation->setValue(0.0);
}

void TaskHatch::setUiEdit()
{
    setWindowTitle(QObject::tr("Edit Face Hatch"));
    ui->fcFile->setFileName(QString::fromStdString(m_saveFile));
    ui->fcFile->setFilter(QStringLiteral(
            "SVG files (*.svg *.SVG);;Bitmap files(*.jpg *.jpeg *.png *.bmp);;All files (*)"));
    ui->sbScale->setValue(m_saveScale);
    ui->sbScale->setSingleStep(0.1);
    ui->ccColor->setColor(m_saveColor.asValue<QColor>());
    ui->dsbRotation->setValue(m_saveRotation);
    ui->dsbOffsetX->setValue(m_saveOffset.x);
    ui->dsbOffsetY->setValue(m_saveOffset.y);
}

void TaskHatch::saveHatchState()
{
    m_saveFile = m_hatch->HatchPattern.getValue();
    m_saveScale = m_vp->HatchScale.getValue();
    m_saveColor = m_vp->HatchColor.getValue();
    m_saveRotation = m_vp->HatchRotation.getValue();
    m_saveOffset = m_vp->HatchOffset.getValue();

}

//restore the start conditions
void TaskHatch::restoreHatchState()
{
//    Base::Console().message("TH::restoreHatchState()\n");
    if (m_hatch) {
        m_hatch->HatchPattern.setValue(m_saveFile);
        m_vp->HatchScale.setValue(m_saveScale);
        m_vp->HatchColor.setValue(m_saveColor);
        m_vp->HatchRotation.setValue(m_saveRotation);
        m_vp->HatchOffset.setValue(m_saveOffset);
    }
}

void TaskHatch::onFileChanged()
{
    m_file = ui->fcFile->fileName().toStdString();
    apply();
}

void TaskHatch::onScaleChanged()
{
    m_scale = ui->sbScale->value().getValue();
    apply();
}

void TaskHatch::onColorChanged()
{
    m_color.setValue<QColor>(ui->ccColor->color());
    apply();
}

void TaskHatch::onRotationChanged()
{
    m_rotation = ui->dsbRotation->value();
    apply();
}

void TaskHatch::onOffsetChanged()
{
    m_offset.x = ui->dsbOffsetX->value();
    m_offset.y = ui->dsbOffsetY->value();
    apply();
}

void TaskHatch::apply(bool forceUpdate)
{
    Q_UNUSED(forceUpdate)
//    Base::Console().message("TH::apply() - m_hatch: %X\n", m_hatch);
    if (!m_hatch) {
        createHatch();
    }
    if (m_hatch) {
        updateHatch();
    }

    if (m_dvp) {
        //only need requestPaint to hatch the face
        //need a recompute in order to claimChildren in tree
        m_dvp->recomputeFeature();
    }
}

void TaskHatch::createHatch()
{
//    Base::Console().message("TH::createHatch()\n");
    App::Document* doc = m_dvp->getDocument();

    // TODO: the structured label for Hatch (and GeomHatch) should be retired.
    const std::string objectName("Hatch");
    std::string FeatName = doc->getUniqueObjectName(objectName.c_str());

    Command::openCommand(QT_TRANSLATE_NOOP("Command", "Create Hatch"));

    Command::doCommand(Command::Doc, "App.activeDocument().addObject('TechDraw::DrawHatch', '%s')", FeatName.c_str());
    Command::doCommand(Command::Doc, "App.activeDocument().%s.translateLabel('DrawHatch', 'Hatch', '%s')",
              FeatName.c_str(), FeatName.c_str());

    m_hatch = static_cast<TechDraw::DrawHatch *>(doc->getObject(FeatName.c_str()));
    m_hatch->Source.setValue(m_dvp, m_subs);

    auto filespec = ui->fcFile->fileName().toStdString();
    filespec = DU::cleanFilespecBackslash(filespec);
    Command::doCommand(Command::Doc, "App.activeDocument().%s.HatchPattern = '%s'",
                       FeatName.c_str(),
                       filespec.c_str());

    //view provider properties
    Gui::ViewProvider* vp = Gui::Application::Instance->getDocument(doc)->getViewProvider(m_hatch);
    m_vp = dynamic_cast<TechDrawGui::ViewProviderHatch*>(vp);
    if (m_vp) {
        Base::Color ac;
        ac.setValue<QColor>(ui->ccColor->color());
        m_vp->HatchColor.setValue(ac);
        m_vp->HatchScale.setValue(ui->sbScale->value().getValue());
        m_vp->HatchRotation.setValue(ui->dsbRotation->value());
        Base::Vector3d offset(ui->dsbOffsetX->value(), ui->dsbOffsetY->value(), 0.0);
        m_vp->HatchOffset.setValue(offset);
    } else {
        Base::Console().error("TaskHatch - Hatch has no ViewProvider\n");
    }
    Command::commitCommand();
}

void TaskHatch::updateHatch()
{
//    Base::Console().message("TH::updateHatch()\n");
    std::string FeatName = m_hatch->getNameInDocument();

    Command::openCommand(QT_TRANSLATE_NOOP("Command", "Update Hatch"));

    auto filespec = ui->fcFile->fileName().toStdString();
    filespec = DU::cleanFilespecBackslash(filespec);
    Command::doCommand(Command::Doc, "App.activeDocument().%s.HatchPattern = '%s'",
                       FeatName.c_str(),
                       filespec.c_str());

    Base::Color ac;
    ac.setValue<QColor>(ui->ccColor->color());
    m_vp->HatchColor.setValue(ac);
    m_vp->HatchScale.setValue(ui->sbScale->value().getValue());
    m_vp->HatchRotation.setValue(ui->dsbRotation->value());
    Base::Vector3d offset(ui->dsbOffsetX->value(), ui->dsbOffsetY->value(), 0.0);
    m_vp->HatchOffset.setValue(offset);
    Command::commitCommand();
}

bool TaskHatch::accept()
{
//    Base::Console().message("TH::accept()\n");
    apply(true);

    Gui::Command::doCommand(Gui::Command::Gui, "Gui.ActiveDocument.resetEdit()");

    return true;
}

bool TaskHatch::reject()
{
//    Base::Console().message("TH::reject()\n");
    restoreHatchState();
    Gui::Command::doCommand(Gui::Command::Gui, "Gui.ActiveDocument.resetEdit()");
    return false;
}

void TaskHatch::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
TaskDlgHatch::TaskDlgHatch(TechDraw::DrawViewPart* inDvp, std::vector<std::string> subs) :
    TaskDialog()
{
    widget  = new TaskHatch(inDvp, subs);
    taskbox = new Gui::TaskView::TaskBox(Gui::BitmapFactory().pixmap("TechDraw_TreeHatch"),
                                         widget->windowTitle(), true, 0);
    taskbox->groupLayout()->addWidget(widget);
    Content.push_back(taskbox);
}

TaskDlgHatch::TaskDlgHatch(TechDrawGui::ViewProviderHatch* inVp) :
    TaskDialog()
{
    widget  = new TaskHatch(inVp);
    taskbox = new Gui::TaskView::TaskBox(Gui::BitmapFactory().pixmap("TechDraw_TreeHatch"),
                                         widget->windowTitle(), true, 0);
    taskbox->groupLayout()->addWidget(widget);
    Content.push_back(taskbox);
}

TaskDlgHatch::~TaskDlgHatch()
{
}

void TaskDlgHatch::update()
{
    //widget->updateTask();
}

//==== calls from the TaskView ===============================================================
void TaskDlgHatch::open()
{
}

void TaskDlgHatch::clicked(int i)
{
    Q_UNUSED(i);
}

bool TaskDlgHatch::accept()
{
    widget->accept();
    return true;
}

bool TaskDlgHatch::reject()
{
    widget->reject();
    return true;
}

#include <Mod/TechDraw/Gui/moc_TaskHatch.cpp>
