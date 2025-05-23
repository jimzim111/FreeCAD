/***************************************************************************
 *   Copyright (c) 2008 Jürgen Riegel <juergen.riegel@web.de>              *
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
# include <limits>
# include <sstream>
# include <boost/regex.hpp>

# include <BRep_Tool.hxx>
# include <BRepAlgo_NormalProjection.hxx>
# include <BRepBndLib.hxx>
# include <BRepBuilderAPI_Copy.hxx>
# include <BRepBuilderAPI_MakeVertex.hxx>
# include <BRepBuilderAPI_Transform.hxx>
# include <BRepClass3d_SolidClassifier.hxx>
# include <BRepExtrema_DistShapeShape.hxx>
# include <BRepExtrema_ShapeProximity.hxx>
# include <BRepExtrema_SupportType.hxx>
# include <BRepFilletAPI_MakeChamfer.hxx>
# include <BRepFilletAPI_MakeFillet.hxx>
# include <BRepGProp.hxx>
# include <BRepMesh_IncrementalMesh.hxx>
# include <BRepProj_Projection.hxx>
# include <BRepTools.hxx>
# include <Geom_Plane.hxx>
# include <gp_Ax1.hxx>
# include <gp_Ax2.hxx>
# include <gp_Dir.hxx>
# include <gp_Pln.hxx>
# include <gp_Pnt.hxx>
# include <gp_Trsf.hxx>
# include <GProp_GProps.hxx>
# include <HLRAppli_ReflectLines.hxx>
# include <Precision.hxx>
# include <Poly_Polygon3D.hxx>
# include <Poly_Triangulation.hxx>
# include <ShapeAnalysis_ShapeTolerance.hxx>
# include <ShapeFix_ShapeTolerance.hxx>
# include <Standard_Version.hxx>
# include <TopExp.hxx>
# include <TopExp_Explorer.hxx>
# include <TopLoc_Location.hxx>
# include <TopoDS.hxx>
# include <TopoDS_Iterator.hxx>
# include <TopTools_IndexedMapOfShape.hxx>
# include <TopTools_ListIteratorOfListOfShape.hxx>
# include <TopTools_ListOfShape.hxx>
#endif

#include <App/PropertyStandard.h>
#include <App/StringHasherPy.h>
#include <Base/FileInfo.h>
#include <Base/GeometryPyCXX.h>
#include <Base/MatrixPy.h>
#include <Base/PyWrapParseTupleAndKeywords.h>
#include <Base/Rotation.h>
#include <Base/Stream.h>
#include <Base/Tools.h>
#include <Base/Vector3D.h>
#include <Base/VectorPy.h>

#include <Mod/Part/App/TopoShapePy.h>
#include <Mod/Part/App/TopoShapePy.cpp>

#include <Mod/Part/App/GeometryPy.h>
#include <Mod/Part/App/PlanePy.h>
#include <Mod/Part/App/TopoShapeCompoundPy.h>
#include <Mod/Part/App/TopoShapeCompSolidPy.h>
#include <Mod/Part/App/TopoShapeEdgePy.h>
#include <Mod/Part/App/TopoShapeFacePy.h>
#include <Mod/Part/App/TopoShapeOpCode.h>
#include <Mod/Part/App/TopoShapeShellPy.h>
#include <Mod/Part/App/TopoShapeSolidPy.h>
#include <Mod/Part/App/TopoShapeVertexPy.h>
#include <Mod/Part/App/TopoShapeWirePy.h>

#include "OCCError.h"
#include "PartPyCXX.h"
#include "ShapeMapHasher.h"
#include "TopoShapeMapper.h"


using namespace Part;

static Py_hash_t _TopoShapeHash(PyObject* self)
{
    if (!self) {
        PyErr_SetString(PyExc_TypeError,
                        "descriptor 'hash' of 'Part.TopoShape' object needs an argument");
        return 0;
    }
    if (!static_cast<Base::PyObjectBase*>(self)->isValid()) {
        PyErr_SetString(PyExc_ReferenceError,
                        "This object is already deleted most likely through closing a document. "
                        "This reference is no longer valid!");
        return 0;
    }
#if OCC_VERSION_HEX >= 0x070800
    return std::hash<TopoDS_Shape> {}(static_cast<TopoShapePy*>(self)->getTopoShapePtr()->getShape());
#else
    return static_cast<TopoShapePy*>(self)->getTopoShapePtr()->getShape().HashCode(
        std::numeric_limits<int>::max());
#endif
}

struct TopoShapePyInit
{
    TopoShapePyInit()
    {
        TopoShapePy::Type.tp_hash = _TopoShapeHash;
    }
} _TopoShapePyInit;

// returns a string which represents the object e.g. when printed in python
std::string TopoShapePy::representation() const
{
    std::stringstream str;
    str << "<Shape object at " << getTopoShapePtr() << ">";

    return str.str();
}

PyObject *TopoShapePy::PyMake(struct _typeobject *, PyObject *, PyObject *)  // Python wrapper
{
    // create a new instance of TopoShapePy and the Twin object
    return new TopoShapePy(new TopoShape);
}

int TopoShapePy::PyInit(PyObject* args, PyObject* keywds)
{
    static const std::array<const char*, 5> kwlist{ "shape",
                                                    "op",
                                                    "tag",
                                                    "hasher",
                                                    nullptr };
    long tag = 0;
    PyObject* pyHasher = nullptr;
    const char* op = nullptr;
    PyObject* pcObj = nullptr;
    if (!Base::Wrapped_ParseTupleAndKeywords(args,
                                             keywds,
                                             "|OsiO!",
                                             kwlist,
                                             &pcObj,
                                             &op,
                                             &tag,
                                             &App::StringHasherPy::Type,
                                             &pyHasher)) {
        return -1;
    }
    auto& self = *getTopoShapePtr();
    self.Tag = tag;
    if (pyHasher) {
        self.Hasher = static_cast<App::StringHasherPy*>(pyHasher)->getStringHasherPtr();
    }
    auto shapes = getPyShapes(pcObj);
    PY_TRY
    {
        if (shapes.size() == 1 && !op) {
            auto s = shapes.front();
            if (self.Tag) {
                if ((s.Tag && self.Tag != s.Tag)
                    || (self.Hasher && s.getElementMapSize() && self.Hasher != s.Hasher)) {
                    s.reTagElementMap(self.Tag, self.Hasher);
                }
                else {
                    s.Tag = self.Tag;
                    s.Hasher = self.Hasher;
                }
            }
            self = s;
        }
        else if (shapes.size()) {
            if (!op) {
                op = Part::OpCodes::Fuse;
            }
            self.makeElementBoolean(op, shapes);
        }
    }
    _PY_CATCH_OCC(return (-1))
    return 0;
}

PyObject* TopoShapePy::copy(PyObject *args) const
{
    PyObject* copyGeom = Py_True;
    PyObject* copyMesh = Py_False;
    const char* op = nullptr;
    PyObject* pyHasher = nullptr;
    if (!PyArg_ParseTuple(args,
                          "|sO!O!O!",
                          &op,
                          &App::StringHasherPy::Type,
                          &pyHasher,
                          &PyBool_Type,
                          &copyGeom,
                          &PyBool_Type,
                          &copyMesh)) {
        PyErr_Clear();
        if (!PyArg_ParseTuple(args, "|O!O!", &PyBool_Type, &copyGeom, &PyBool_Type, &copyMesh)) {
            return 0;
        }
    }
    if (op && !op[0]) {
        op = nullptr;
    }
    App::StringHasherRef hasher;
    if (pyHasher) {
        hasher = static_cast<App::StringHasherPy*>(pyHasher)->getStringHasherPtr();
    }
    auto& self = *getTopoShapePtr();
    return Py::new_reference_to(shape2pyshape(
        TopoShape(self.Tag, hasher)
            .makeElementCopy(self, op, PyObject_IsTrue(copyGeom), PyObject_IsTrue(copyMesh))));
}

PyObject* TopoShapePy::cleaned(PyObject *args) const
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;
    auto& self = *getTopoShapePtr();
    TopoShape copy(self.makeElementCopy());
    if (!copy.isNull()) {
        BRepTools::Clean(copy.getShape());  // remove triangulation
    }
    return Py::new_reference_to(shape2pyshape(copy));
}

PyObject* TopoShapePy::replaceShape(PyObject *args) const
{
    PyObject *l;
    if (!PyArg_ParseTuple(args, "O",&l))
        return nullptr;

    try {
        Py::Sequence list(l);
        std::vector<std::pair<TopoShape, TopoShape>> shapes;
        for (Py::Sequence::iterator it = list.begin(); it != list.end(); ++it) {
            Py::Tuple tuple(*it);
            Py::TopoShape sh1(tuple[0]);
            Py::TopoShape sh2(tuple[1]);
            shapes.push_back(std::make_pair(*sh1.extensionObject()->getTopoShapePtr(),
                                            *sh2.extensionObject()->getTopoShapePtr()));
        }
        return Py::new_reference_to(shape2pyshape(getTopoShapePtr()->replaceElementShape(shapes)));
    }
    catch (const Py::Exception&) {
        return nullptr;
    }
    catch (...) {
        PyErr_SetString(PartExceptionOCCError, "failed to replace shape");
        return nullptr;
    }
}

PyObject* TopoShapePy::removeShape(PyObject *args) const
{
    PyObject *l;
    if (!PyArg_ParseTuple(args, "O",&l))
        return nullptr;

    try {
        return Py::new_reference_to(
            shape2pyshape(getTopoShapePtr()->removeElementShape(getPyShapes(l))));
    }
    catch (...) {
        PyErr_SetString(PartExceptionOCCError, "failed to remove shape");
        return nullptr;
    }
}

PyObject*  TopoShapePy::read(PyObject *args)
{
    char* Name;
    if (!PyArg_ParseTuple(args, "et","utf-8",&Name))
        return nullptr;

    std::string EncodedName = std::string(Name);
    PyMem_Free(Name);

    getTopoShapePtr()->read(EncodedName.c_str());
    Py_Return;
}

PyObject* TopoShapePy::writeInventor(PyObject * args, PyObject * keywds) const
{
    static const std::array<const char *, 5> kwlist{"Mode", "Deviation", "Angle", "FaceColors", nullptr};

    double dev = 0.3, angle = 0.4;
    int mode = 2;
    PyObject *pylist = nullptr;
    if (!Base::Wrapped_ParseTupleAndKeywords(args, keywds, "|iddO", kwlist,
                                             &mode, &dev, &angle, &pylist)) {
        return nullptr;
    }

    std::vector<Base::Color> faceColors;
    if (pylist) {
        App::PropertyColorList prop;
        prop.setPyObject(pylist);
        faceColors = prop.getValues();
    }

    std::stringstream result;
    BRepMesh_IncrementalMesh(getTopoShapePtr()->getShape(),dev);
    if (mode == 0) {
        getTopoShapePtr()->exportFaceSet(dev, angle, faceColors, result);
    }
    else if (mode == 1) {
        getTopoShapePtr()->exportLineSet(result);
    }
    else {
        getTopoShapePtr()->exportFaceSet(dev, angle, faceColors, result);
        getTopoShapePtr()->exportLineSet(result);
    }
    return Py::new_reference_to(Py::String(result.str()));
}

PyObject*  TopoShapePy::exportIges(PyObject *args) const
{
    char* Name;
    if (!PyArg_ParseTuple(args, "et","utf-8",&Name))
        return nullptr;

    std::string EncodedName = std::string(Name);
    PyMem_Free(Name);

    try {
        // write iges file
        getTopoShapePtr()->exportIges(EncodedName.c_str());
    }
    catch (const Base::Exception& e) {
        PyErr_SetString(PartExceptionOCCError,e.what());
        return nullptr;
    }

    Py_Return;
}

PyObject*  TopoShapePy::exportStep(PyObject *args) const
{
    char* Name;
    if (!PyArg_ParseTuple(args, "et","utf-8",&Name))
        return nullptr;

    std::string EncodedName = std::string(Name);
    PyMem_Free(Name);

    try {
        // write step file
        getTopoShapePtr()->exportStep(EncodedName.c_str());
    }
    catch (const Base::Exception& e) {
        PyErr_SetString(PartExceptionOCCError,e.what());
        return nullptr;
    }

    Py_Return;
}

PyObject*  TopoShapePy::exportBrep(PyObject *args) const
{
    char* Name;
    if (PyArg_ParseTuple(args, "et","utf-8",&Name)) {
        std::string EncodedName = std::string(Name);
        PyMem_Free(Name);

        try {
            // write brep file
            getTopoShapePtr()->exportBrep(EncodedName.c_str());
        }
        catch (const Base::Exception& e) {
            PyErr_SetString(PartExceptionOCCError,e.what());
            return nullptr;
        }

        Py_Return;
    }

    PyErr_Clear();

    PyObject* input;
    if (PyArg_ParseTuple(args, "O", &input)) {
        try {
            // write brep
            Base::PyStreambuf buf(input);
            std::ostream str(nullptr);
            str.rdbuf(&buf);
            getTopoShapePtr()->exportBrep(str);
        }
        catch (const Base::Exception& e) {
            PyErr_SetString(PartExceptionOCCError,e.what());
            return nullptr;
        }

        Py_Return;
    }

    PyErr_SetString(PyExc_TypeError, "expect string or file object");
    return nullptr;
}

PyObject*  TopoShapePy::exportBinary(PyObject *args) const
{
    char* input;
    if (!PyArg_ParseTuple(args, "s", &input))
        return nullptr;

    try {
        // read binary brep
        Base::FileInfo fi(input);
        Base::ofstream str(fi, std::ios::out | std::ios::binary);
        getTopoShapePtr()->exportBinary(str);
        str.close();
    }
    catch (const Base::Exception& e) {
        PyErr_SetString(PartExceptionOCCError,e.what());
        return nullptr;
    }

    Py_Return;
}

PyObject*  TopoShapePy::dumpToString(PyObject *args) const
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;

    try {
        std::stringstream str;
        getTopoShapePtr()->dump(str);
        return Py::new_reference_to(Py::String(str.str()));
    }
    catch (const Base::Exception& e) {
        PyErr_SetString(PartExceptionOCCError,e.what());
        return nullptr;
    }
    catch (const std::exception& e) {
        PyErr_SetString(PartExceptionOCCError,e.what());
        return nullptr;
    }
    catch (Standard_Failure& e) {

        PyErr_SetString(PartExceptionOCCError, e.GetMessageString());
        return nullptr;
    }
}

PyObject*  TopoShapePy::exportBrepToString(PyObject *args) const
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;

    try {
        // write brep file
        std::stringstream str;
        getTopoShapePtr()->exportBrep(str);
        return Py::new_reference_to(Py::String(str.str()));
    }
    catch (const Base::Exception& e) {
        PyErr_SetString(PartExceptionOCCError,e.what());
        return nullptr;
    }
    catch (const std::exception& e) {
        PyErr_SetString(PartExceptionOCCError,e.what());
        return nullptr;
    }
    catch (Standard_Failure& e) {
        PyErr_SetString(PartExceptionOCCError, e.GetMessageString());
        return nullptr;
    }
}

PyObject*  TopoShapePy::importBrep(PyObject *args)
{
    char* Name;
    if (PyArg_ParseTuple(args, "et","utf-8",&Name)) {
        std::string EncodedName = std::string(Name);
        PyMem_Free(Name);

        try {
            // write brep file
            getTopoShapePtr()->importBrep(EncodedName.c_str());
        }
        catch (const Base::Exception& e) {
            PyErr_SetString(PartExceptionOCCError,e.what());
            return nullptr;
        }

        Py_Return;
    }

    PyErr_Clear();
    PyObject* input;
    if (PyArg_ParseTuple(args, "O", &input)) {
        try {
            // read brep
            Base::PyStreambuf buf(input);
            std::istream str(nullptr);
            str.rdbuf(&buf);
            getTopoShapePtr()->importBrep(str);
        }
        catch (const Base::Exception& e) {
            PyErr_SetString(PartExceptionOCCError,e.what());
            return nullptr;
        }

        Py_Return;
    }

    PyErr_SetString(PyExc_TypeError, "expect string or file object");
    return nullptr;
}

PyObject*  TopoShapePy::importBinary(PyObject *args)
{
    char* input;
    if (!PyArg_ParseTuple(args, "s", &input))
        return nullptr;

    try {
        // read binary brep
        Base::FileInfo fi(input);
        Base::ifstream str(fi, std::ios::in | std::ios::binary);
        getTopoShapePtr()->importBinary(str);
        str.close();
    }
    catch (const Base::Exception& e) {
        PyErr_SetString(PartExceptionOCCError,e.what());
        return nullptr;
    }

    Py_Return;
}

PyObject*  TopoShapePy::importBrepFromString(PyObject *args)
{
    char* input;
    int indicator=1;
    if (!PyArg_ParseTuple(args, "s|i", &input, &indicator))
        return nullptr;

    try {
        // read brep
        std::stringstream str(input);
        getTopoShapePtr()->importBrep(str,indicator);
    }
    catch (const Base::Exception& e) {
        PyErr_SetString(PartExceptionOCCError,e.what());
        return nullptr;
    }
    catch (const std::exception& e) {
        PyErr_SetString(PartExceptionOCCError,e.what());
        return nullptr;
    }
    catch (Standard_Failure& e) {
        PyErr_SetString(PartExceptionOCCError, e.GetMessageString());
        return nullptr;
    }

    Py_Return;
}

PyObject* TopoShapePy::dumps(PyObject* args) const
{
    return exportBrepToString(args);
}


PyObject*  TopoShapePy::loads(PyObject *args) {
    if (! getTopoShapePtr()) {
        PyErr_SetString(Base::PyExc_FC_GeneralError,"no c++ object");
        return nullptr;
    }
    else {
        return importBrepFromString(args);
    }
}

PyObject*  TopoShapePy::exportStl(PyObject *args) const
{
    double deflection = 0.01;
    char* Name;
    if (!PyArg_ParseTuple(args, "et|d","utf-8",&Name,&deflection))
        return nullptr;

    std::string EncodedName = std::string(Name);
    PyMem_Free(Name);

    try {
        // write stl file
        getTopoShapePtr()->exportStl(EncodedName.c_str(), deflection);
    }
    catch (const Base::Exception& e) {
        PyErr_SetString(PartExceptionOCCError,e.what());
        return nullptr;
    }
    catch (Standard_Failure& e) {
        PyErr_SetString(PartExceptionOCCError, e.GetMessageString());
        return nullptr;
    }

    Py_Return;
}

PyObject* TopoShapePy::extrude(PyObject *args) const
{
    PyObject *pVec;
    if (!PyArg_ParseTuple(args, "O!", &(Base::VectorPy::Type), &pVec))
        return nullptr;

    try {
        Base::Vector3d vec = static_cast<Base::VectorPy*>(pVec)->value();
        return Py::new_reference_to(
            shape2pyshape(getTopoShapePtr()->makeElementPrism(gp_Vec(vec.x, vec.y, vec.z))));
    }
    catch (Standard_Failure& e) {
        PyErr_SetString(PartExceptionOCCError, e.GetMessageString());
        return nullptr;
    }
}

PyObject* TopoShapePy::revolve(PyObject *args) const
{
    PyObject *pPos,*pDir;
    double angle=360;
    if (!PyArg_ParseTuple(args, "O!O!|d", &(Base::VectorPy::Type), &pPos, &(Base::VectorPy::Type), &pDir,&angle))
        return nullptr;
    Base::Vector3d pos = static_cast<Base::VectorPy*>(pPos)->value();
    Base::Vector3d dir = static_cast<Base::VectorPy*>(pDir)->value();
    try {
        return Py::new_reference_to(shape2pyshape(getTopoShapePtr()->makeElementRevolve(
            gp_Ax1(gp_Pnt(pos.x, pos.y, pos.z), gp_Dir(dir.x, dir.y, dir.z)),
            Base::toRadians<double>(angle))));
    }
    catch (Standard_Failure& e) {
        PyErr_SetString(PartExceptionOCCError, e.GetMessageString());
        return nullptr;
    }
}

PyObject*  TopoShapePy::check(PyObject *args) const
{
    PyObject* runBopCheck = Py_False;
    if (!PyArg_ParseTuple(args, "|O!", &(PyBool_Type), &runBopCheck))
        return nullptr;

    if (!getTopoShapePtr()->getShape().IsNull()) {
        std::stringstream str;
        if (!getTopoShapePtr()->analyze(Base::asBoolean(runBopCheck), str)) {
            PyErr_SetString(PyExc_ValueError, str.str().c_str());
            return nullptr;
        }
    }

    Py_Return;
}

static PyObject *makeShape(const char *op,const TopoShape &shape, PyObject *args) {
    double tol=0;
    PyObject *pcObj;
    if (!PyArg_ParseTuple(args, "O|d", &pcObj,&tol))
        return 0;
    PY_TRY {
        std::vector<TopoShape> shapes;
        shapes.push_back(shape);
        getPyShapes(pcObj,shapes);
        return Py::new_reference_to(shape2pyshape(TopoShape().makeElementBoolean(op,shapes,0,tol)));
    } PY_CATCH_OCC
}

PyObject*  TopoShapePy::fuse(PyObject *args) const
{
    return makeShape(Part::OpCodes::Fuse, *getTopoShapePtr(), args);
}

PyObject*  TopoShapePy::multiFuse(PyObject *args) const
{
    return makeShape(Part::OpCodes::Fuse, *getTopoShapePtr(), args);
}

PyObject*  TopoShapePy::oldFuse(PyObject *args) const
{
    PyObject *pcObj;
    if (!PyArg_ParseTuple(args, "O!", &(TopoShapePy::Type), &pcObj))
        return nullptr;

    TopoDS_Shape shape = static_cast<TopoShapePy*>(pcObj)->getTopoShapePtr()->getShape();
    try {
        // Let's call algorithm computing a fuse operation:
        TopoDS_Shape fusShape = this->getTopoShapePtr()->oldFuse(shape);
        return new TopoShapePy(new TopoShape(fusShape));
    }
    catch (Standard_Failure& e) {
        PyErr_SetString(PartExceptionOCCError, e.GetMessageString());
        return nullptr;
    }
    catch (const std::exception& e) {
        PyErr_SetString(PartExceptionOCCError, e.what());
        return nullptr;
    }
}

PyObject*  TopoShapePy::common(PyObject *args) const
{
    return makeShape(Part::OpCodes::Common, *getTopoShapePtr(), args);
}

PyObject*  TopoShapePy::section(PyObject *args) const
{
    return makeShape(Part::OpCodes::Section, *getTopoShapePtr(), args);
}

PyObject*  TopoShapePy::slice(PyObject *args) const
{
    PyObject *dir;
    double d;
    if (!PyArg_ParseTuple(args, "O!d", &(Base::VectorPy::Type), &dir, &d))
        return nullptr;

    Base::Vector3d vec = Py::Vector(dir, false).toVector();

    try {
        Py::List wires;
        for (auto& w : getTopoShapePtr()->makeElementSlice(vec, d).getSubTopoShapes(TopAbs_WIRE)) {
            wires.append(shape2pyshape(w));
        }
        return Py::new_reference_to(wires);
    }
    catch (Standard_Failure& e) {

        PyErr_SetString(PartExceptionOCCError, e.GetMessageString());
        return nullptr;
    }
    catch (const std::exception& e) {
        PyErr_SetString(PartExceptionOCCError, e.what());
        return nullptr;
    }
}

PyObject*  TopoShapePy::slices(PyObject *args) const
{
    PyObject *dir, *dist;
    if (!PyArg_ParseTuple(args, "O!O", &(Base::VectorPy::Type), &dir, &dist))
        return nullptr;

    try {
        Base::Vector3d vec = Py::Vector(dir, false).toVector();
        Py::Sequence list(dist);
        std::vector<double> d;
        d.reserve(list.size());
        for (Py::Sequence::iterator it = list.begin(); it != list.end(); ++it)
            d.push_back((double)Py::Float(*it));
        return Py::new_reference_to(shape2pyshape(getTopoShapePtr()->makeElementSlices(vec, d)));
    }
    catch (Standard_Failure& e) {
        PyErr_SetString(PartExceptionOCCError, e.GetMessageString());
        return nullptr;
    }
    catch (const std::exception& e) {
        PyErr_SetString(PartExceptionOCCError, e.what());
        return nullptr;
    }
}

PyObject*  TopoShapePy::cut(PyObject *args) const
{
    return makeShape(Part::OpCodes::Cut, *getTopoShapePtr(), args);
}

PyObject*  TopoShapePy::generalFuse(PyObject *args) const
{
    double tolerance = 0.0;
    PyObject *pcObj;
    if (!PyArg_ParseTuple(args, "O|d", &pcObj, &tolerance))
        return nullptr;

    std::vector<std::vector<TopoShape>> modifies;
    std::vector<TopoShape> shapes;
    shapes.push_back(*getTopoShapePtr());
    try {
        getPyShapes(pcObj, shapes);
        TopoShape res;
        res.makeElementGeneralFuse(shapes, modifies, tolerance);
        Py::List mapPy;
        for (auto& mod : modifies) {
            Py::List shapesPy;
            for (auto& sh : mod) {
                shapesPy.append(shape2pyshape(sh));
            }
            mapPy.append(shapesPy);
        }
        Py::Tuple ret(2);
        ret[0] = shape2pyshape(res);
        ret[1] = mapPy;
        return Py::new_reference_to(ret);
    }
    PY_CATCH_OCC
}

PyObject*  TopoShapePy::sewShape(PyObject *args)
{
    double tolerance = 1.0e-06;
    if (!PyArg_ParseTuple(args, "|d", &tolerance))
        return nullptr;

    try {
        getTopoShapePtr()->sewShape();
        Py_Return;
    }
    catch (Standard_Failure& e) {
        PyErr_SetString(PartExceptionOCCError, e.GetMessageString());
        return nullptr;
    }
}

PyObject* TopoShapePy::childShapes(PyObject *args) const
{
    PyObject* cumOri = Py_True;
    PyObject* cumLoc = Py_True;
    if (!PyArg_ParseTuple(args, "|O!O!", &(PyBool_Type), &cumOri, &(PyBool_Type), &cumLoc))
        return nullptr;

    TopoShape shape = *getTopoShapePtr();
    if (!PyObject_IsTrue(cumOri)) {
        shape.setShape(shape.getShape().Oriented(TopAbs_FORWARD), false);
    }
    if (!PyObject_IsTrue(cumLoc)) {
        shape.setShape(shape.getShape().Located(TopLoc_Location()), false);
    }
    Py::List list;
    PY_TRY
    {
        for (auto& s : shape.getSubTopoShapes()) {
            list.append(shape2pyshape(s));
        }
        return Py::new_reference_to(list);
    }
    PY_CATCH_OCC
}

namespace Part {
// Containers to associate TopAbs_ShapeEnum values to each TopoShape*Py class
static const std::vector<std::pair<PyTypeObject*, TopAbs_ShapeEnum>> vecTypeShape = {
    {&TopoShapeCompoundPy::Type, TopAbs_COMPOUND},
    {&TopoShapeCompSolidPy::Type, TopAbs_COMPSOLID},
    {&TopoShapeSolidPy::Type, TopAbs_SOLID},
    {&TopoShapeShellPy::Type, TopAbs_SHELL},
    {&TopoShapeFacePy::Type, TopAbs_FACE},
    {&TopoShapeWirePy::Type, TopAbs_WIRE},
    {&TopoShapeEdgePy::Type, TopAbs_EDGE},
    {&TopoShapeVertexPy::Type, TopAbs_VERTEX},
    {&TopoShapePy::Type, TopAbs_SHAPE}
};

static const std::map<PyTypeObject*, TopAbs_ShapeEnum> mapTypeShape(
    vecTypeShape.begin(), vecTypeShape.end());

// Returns shape type of a Python type. Similar to TopAbs::ShapeTypeFromString.
// Returns TopAbs_SHAPE if pyType is not a subclass of any of the TopoShape*Py.
static TopAbs_ShapeEnum ShapeTypeFromPyType(PyTypeObject* pyType)
{
    for (const auto & it : vecTypeShape) {
        if (PyType_IsSubtype(pyType, it.first))
            return it.second;
    }
    return TopAbs_SHAPE;
}
}

PyObject*  TopoShapePy::ancestorsOfType(PyObject *args) const
{
    PyObject *pcObj;
    PyObject *type;
    if (!PyArg_ParseTuple(args, "O!O!", &(TopoShapePy::Type), &pcObj, &PyType_Type, &type))
        return nullptr;

    try {
        const TopoDS_Shape& model = getTopoShapePtr()->getShape();
        const TopoDS_Shape& shape = static_cast<TopoShapePy*>(pcObj)->
                getTopoShapePtr()->getShape();
        if (model.IsNull() || shape.IsNull()) {
            PyErr_SetString(PyExc_ValueError, "Shape is null");
            return nullptr;
        }

        PyTypeObject* pyType = reinterpret_cast<PyTypeObject*>(type);
        TopAbs_ShapeEnum shapetype = ShapeTypeFromPyType(pyType);
        if (!PyType_IsSubtype(pyType, &TopoShapePy::Type)) {
            PyErr_SetString(PyExc_TypeError, "type must be a Shape subtype");
            return nullptr;
        }

        TopTools_IndexedDataMapOfShapeListOfShape mapOfShapeShape;
        TopExp::MapShapesAndAncestors(model, shape.ShapeType(), shapetype, mapOfShapeShape);
        const TopTools_ListOfShape& ancestors = mapOfShapeShape.FindFromKey(shape);

        Py::List list;
        std::set<Standard_Integer> hashes;
        TopTools_ListIteratorOfListOfShape it(ancestors);
        for (; it.More(); it.Next()) {
            // make sure to avoid duplicates
            Standard_Integer code = ShapeMapHasher{}(it.Value());
            if (hashes.find(code) == hashes.end()) {
                list.append(shape2pyshape(it.Value()));
                hashes.insert(code);
            }
        }

        return Py::new_reference_to(list);
    }
    catch (Standard_Failure& e) {
        PyErr_SetString(PartExceptionOCCError, e.GetMessageString());
        return nullptr;
    }
}

PyObject*  TopoShapePy::removeInternalWires(PyObject *args)
{
    double minArea;
    if (!PyArg_ParseTuple(args, "d",&minArea))
        return nullptr;

    try {
        bool ok = getTopoShapePtr()->removeInternalWires(minArea);
        PyObject* ret = ok ? Py_True : Py_False;
        Py_INCREF(ret);
        return ret;
    }
    catch (Standard_Failure& e) {
        PyErr_SetString(PartExceptionOCCError, e.GetMessageString());
        return nullptr;
    }
}

PyObject*  TopoShapePy::mirror(PyObject *args) const
{
    PyObject *v1, *v2;
    if (!PyArg_ParseTuple(args, "O!O!", &(Base::VectorPy::Type),&v1, &(Base::VectorPy::Type),&v2))
        return nullptr;

    Base::Vector3d base = Py::Vector(v1,false).toVector();
    Base::Vector3d norm = Py::Vector(v2,false).toVector();

    try {
        gp_Ax2 ax2(gp_Pnt(base.x,base.y,base.z), gp_Dir(norm.x,norm.y,norm.z));
        return Py::new_reference_to(shape2pyshape(getTopoShapePtr()->makeElementMirror(ax2)));
    }
    catch (Standard_Failure& e) {
        PyErr_SetString(PartExceptionOCCError, e.GetMessageString());
        return nullptr;
    }
}

PyObject*  TopoShapePy::transformGeometry(PyObject *args) const
{
    PyObject *obj;
    PyObject *cpy = Py_False;
    if (!PyArg_ParseTuple(args, "O!|O!", &(Base::MatrixPy::Type), &obj, &PyBool_Type, &cpy))
        return nullptr;

    try {
        Base::Matrix4D mat = static_cast<Base::MatrixPy*>(obj)->value();
        TopoDS_Shape shape = this->getTopoShapePtr()->transformGShape(mat, Base::asBoolean(cpy));
        return new TopoShapePy(new TopoShape(shape));
    }
    catch (Standard_Failure& e) {
        PyErr_SetString(PartExceptionOCCError, e.GetMessageString());
        return nullptr;
    }
}

PyObject*  TopoShapePy::transformShape(PyObject *args)
{
    PyObject *obj;
    PyObject *copy = Py_False;
    PyObject *checkScale = Py_False;
    if (!PyArg_ParseTuple(args, "O!|O!O!", &(Base::MatrixPy::Type),&obj,&(PyBool_Type), &copy, &(PyBool_Type), &checkScale))
        return nullptr;

    Base::Matrix4D mat = static_cast<Base::MatrixPy*>(obj)->value();
    PY_TRY {
        this->getTopoShapePtr()->transformShape(mat, Base::asBoolean(copy), Base::asBoolean(checkScale));
        return IncRef();
    }
    PY_CATCH_OCC
}

PyObject* TopoShapePy::transformed(PyObject *args, PyObject *keywds) const
{
    static const std::array<const char *, 5> kwlist{"matrix", "copy", "checkScale", "op", nullptr};
    PyObject *pymat;
    PyObject *copy = Py_False;
    PyObject *checkScale = Py_False;
    const char *op = nullptr;
    if (!Base::Wrapped_ParseTupleAndKeywords(args, keywds, "O!|O!O!s", kwlist,
                                             &Base::MatrixPy::Type, &pymat, &PyBool_Type, &copy, &PyBool_Type,
                                             &checkScale, &op)) {
        return nullptr;
    }

    Base::Matrix4D mat = static_cast<Base::MatrixPy*>(pymat)->value();
    (void)op;
    PY_TRY {
        TopoShape s(*getTopoShapePtr());
        s.transformShape(mat,Base::asBoolean(copy), Base::asBoolean(checkScale));
        return Py::new_reference_to(shape2pyshape(s));
    }
    PY_CATCH_OCC
}

PyObject*  TopoShapePy::translate(PyObject *args)
{
    PyObject *obj;
    if (!PyArg_ParseTuple(args, "O", &obj))
        return nullptr;

    Base::Vector3d vec;
    if (PyObject_TypeCheck(obj, &(Base::VectorPy::Type))) {
        vec = static_cast<Base::VectorPy*>(obj)->value();
    }
    else if (PyObject_TypeCheck(obj, &PyTuple_Type)) {
        vec = Base::getVectorFromTuple<double>(obj);
    }
    else {
        PyErr_SetString(PyExc_TypeError, "either vector or tuple expected");
        return nullptr;
    }

    gp_Trsf mov;
    mov.SetTranslation(gp_Vec(vec.x,vec.y,vec.z));
    TopLoc_Location loc(mov);
    TopoDS_Shape shape = getTopoShapePtr()->getShape();
    shape.Move(loc);
    getTopoShapePtr()->setShape(shape);

    return IncRef();
}

PyObject*  TopoShapePy::rotate(PyObject *args)
{
    PyObject *obj1, *obj2;
    double angle;
    if (!PyArg_ParseTuple(args, "OOd", &obj1, &obj2, &angle))
        return nullptr;

    PY_TRY {
        // Vector also supports sequence
        Py::Sequence p1(obj1), p2(obj2);
        // Convert into OCC representation
        gp_Pnt pos = gp_Pnt((double)Py::Float(p1[0]),
                            (double)Py::Float(p1[1]),
                            (double)Py::Float(p1[2]));
        gp_Dir dir = gp_Dir((double)Py::Float(p2[0]),
                            (double)Py::Float(p2[1]),
                            (double)Py::Float(p2[2]));

        gp_Ax1 axis(pos, dir);
        gp_Trsf mov;
        mov.SetRotation(axis, Base::toRadians<double>(angle));
        TopLoc_Location loc(mov);
        TopoDS_Shape shape = getTopoShapePtr()->getShape();
        shape.Move(loc);
        getTopoShapePtr()->setShape(shape);

        return IncRef();
    }
    PY_CATCH_OCC
}

PyObject*  TopoShapePy::scale(PyObject *args)
{
    double factor;
    PyObject* p=nullptr;
    if (!PyArg_ParseTuple(args, "d|O!", &factor, &(Base::VectorPy::Type), &p))
        return nullptr;

    gp_Pnt pos(0,0,0);
    if (p) {
        Base::Vector3d pnt = static_cast<Base::VectorPy*>(p)->value();
        pos.SetX(pnt.x);
        pos.SetY(pnt.y);
        pos.SetZ(pnt.z);
    }
    if (fabs(factor) < Precision::Confusion()) {
        PyErr_SetString(PyExc_ValueError, "scale factor too small");
        return nullptr;
    }

    PY_TRY {
        const TopoDS_Shape& shape = getTopoShapePtr()->getShape();
        if (!shape.IsNull()) {
            gp_Trsf scl;
            scl.SetScale(pos, factor);
            BRepBuilderAPI_Transform BRepScale(scl);
            bool bCopy = true;
            BRepScale.Perform(shape, bCopy);
            TopoShape copy(*getTopoShapePtr());
            getTopoShapePtr()->makeElementShape(BRepScale, copy);
        }
        return IncRef();
    }
    PY_CATCH_OCC
}

PyObject*  TopoShapePy::translated(PyObject *args) const
{
    Py::Object pyobj(shape2pyshape(*getTopoShapePtr()));
    return static_cast<TopoShapePy*>(pyobj.ptr())->translate(args);
}

PyObject*  TopoShapePy::rotated(PyObject *args) const
{
    Py::Object pyobj(shape2pyshape(*getTopoShapePtr()));
    return static_cast<TopoShapePy*>(pyobj.ptr())->rotate(args);
}

PyObject*  TopoShapePy::scaled(PyObject *args) const
{
    Py::Object pyobj(shape2pyshape(*getTopoShapePtr()));
    return static_cast<TopoShapePy*>(pyobj.ptr())->scale(args);
}

PyObject* TopoShapePy::makeFillet(PyObject *args) const
{
    // use two radii for all edges
    double radius1, radius2;
    PyObject *obj;
    if (!PyArg_ParseTuple(args, "ddO", &radius1, &radius2, &obj)) {
        PyErr_Clear();
        if (!PyArg_ParseTuple(args, "dO", &radius1, &obj)) {
            PyErr_SetString(PyExc_TypeError,
                            "This method accepts:\n"
                            "-- one radius and a list of edges\n"
                            "-- two radii and a list of edges");
            return 0;
        }
        radius2 = radius1;
    }
    PY_TRY
    {
        return Py::new_reference_to(shape2pyshape(
            getTopoShapePtr()->makeElementFillet(getPyShapes(obj), radius1, radius2)));
    }
    PY_CATCH_OCC
    PyErr_Clear();
    // use one radius for all edges
    double radius;
    if (PyArg_ParseTuple(args, "dO", &radius, &obj)) {
        try {
            const TopoDS_Shape& shape = this->getTopoShapePtr()->getShape();
            BRepFilletAPI_MakeFillet mkFillet(shape);
            Py::Sequence list(obj);
            for (Py::Sequence::iterator it = list.begin(); it != list.end(); ++it) {
                if (PyObject_TypeCheck((*it).ptr(), &(Part::TopoShapePy::Type))) {
                    const TopoDS_Shape& edge = static_cast<TopoShapePy*>((*it).ptr())->getTopoShapePtr()->getShape();
                    if (edge.ShapeType() == TopAbs_EDGE) {
                        //Add edge to fillet algorithm
                        mkFillet.Add(radius, TopoDS::Edge(edge));
                    }
                }
            }
            return new TopoShapePy(new TopoShape(mkFillet.Shape()));
        }
        catch (Standard_Failure& e) {
            PyErr_SetString(PartExceptionOCCError, e.GetMessageString());
            return nullptr;
        }
    }

    PyErr_SetString(PyExc_TypeError, "This method accepts:\n"
        "-- one radius and a list of edges\n"
        "-- two radii and a list of edges");
    return nullptr;
}

// TODO:  Should this python interface support all three chamfer methods and not just two?
PyObject* TopoShapePy::makeChamfer(PyObject *args) const
{
    // use two radii for all edges
    double radius1, radius2;
    PyObject *obj;
    if (!PyArg_ParseTuple(args, "ddO", &radius1, &radius2, &obj)) {
        if (!PyArg_ParseTuple(args, "dO", &radius1, &obj)) {
            PyErr_SetString(PyExc_TypeError,
                            "This method accepts:\n"
                            "-- one radius and a list of edges\n"
                            "-- two radii and a list of edges");
            return 0;
        }
        PyErr_Clear();
        radius2 = radius1;
    }
    PY_TRY
    {
        return Py::new_reference_to(shape2pyshape(
            getTopoShapePtr()->makeElementChamfer(getPyShapes(obj), Part::ChamferType::twoDistances, radius1, radius2)));
    }
    PY_CATCH_OCC
    PyErr_Clear();
    // use one radius for all edges
    // TODO: Should this be using makeElementChamfer to support Toponaming fixes?
    double radius;
    if (PyArg_ParseTuple(args, "dO", &radius, &obj)) {
        try {
            const TopoDS_Shape& shape = this->getTopoShapePtr()->getShape();
            BRepFilletAPI_MakeChamfer mkChamfer(shape);
            TopTools_IndexedMapOfShape mapOfEdges;
            TopTools_IndexedDataMapOfShapeListOfShape mapEdgeFace;
            TopExp::MapShapesAndAncestors(shape, TopAbs_EDGE, TopAbs_FACE, mapEdgeFace);
            TopExp::MapShapes(shape, TopAbs_EDGE, mapOfEdges);
            Py::Sequence list(obj);
            for (Py::Sequence::iterator it = list.begin(); it != list.end(); ++it) {
                if (PyObject_TypeCheck((*it).ptr(), &(Part::TopoShapePy::Type))) {
                    const TopoDS_Shape& edge = static_cast<TopoShapePy*>((*it).ptr())->getTopoShapePtr()->getShape();
                    if (edge.ShapeType() == TopAbs_EDGE) {
                        //Add edge to fillet algorithm
                        const TopoDS_Face& face = TopoDS::Face(mapEdgeFace.FindFromKey(edge).First());
                        mkChamfer.Add(radius, radius, TopoDS::Edge(edge), face);
                    }
                }
            }
            return new TopoShapePy(new TopoShape(mkChamfer.Shape()));
        }
        catch (Standard_Failure& e) {
            PyErr_SetString(PartExceptionOCCError, e.GetMessageString());
            return nullptr;
        }
    }

    PyErr_SetString(PyExc_TypeError, "This method accepts:\n"
        "-- one radius and a list of edges\n"
        "-- two radii and a list of edges");
    return nullptr;
}

PyObject* TopoShapePy::makeThickness(PyObject *args) const
{
    PyObject *obj;
    double offset, tolerance;
    PyObject* inter = Py_False;
    PyObject* self_inter = Py_False;
    short offsetMode = 0, join = 0;
    if (!PyArg_ParseTuple(args, "Odd|O!O!hh", &obj, &offset, &tolerance,
        &(PyBool_Type), &inter, &(PyBool_Type), &self_inter, &offsetMode, &join))
        return nullptr;

    try {
        return Py::new_reference_to(shape2pyshape(
            getTopoShapePtr()->makeElementThickSolid(getPyShapes(obj),
                                                     offset,
                                                     tolerance,
                                                     PyObject_IsTrue(inter) ? true : false,
                                                     PyObject_IsTrue(self_inter) ? true : false,
                                                     offsetMode,
                                                     static_cast<JoinType>(join))));
    }
    catch (Standard_Failure& e) {
        PyErr_SetString(PartExceptionOCCError, e.GetMessageString());
        return nullptr;
    }
}

PyObject* TopoShapePy::makeOffsetShape(PyObject *args, PyObject *keywds) const
{
    static const std::array<const char *, 8> kwlist{"offset", "tolerance", "inter", "self_inter", "offsetMode", "join",
                                                    "fill", nullptr};
    double offset, tolerance;
    PyObject *inter = Py_False;
    PyObject *self_inter = Py_False;
    PyObject *fill = Py_False;
    short offsetMode = 0, join = 0;
    if (!Base::Wrapped_ParseTupleAndKeywords(args, keywds, "dd|O!O!hhO!", kwlist, &offset, &tolerance,
                                            &(PyBool_Type), &inter, &(PyBool_Type), &self_inter, &offsetMode, &join,
                                            &(PyBool_Type), &fill)) {
        return nullptr;
    }

    try {
        return Py::new_reference_to(shape2pyshape(getTopoShapePtr()->makeElementOffset(
            offset,
            tolerance,
            PyObject_IsTrue(inter) ? true : false,
            PyObject_IsTrue(self_inter) ? true : false,
            offsetMode,
            static_cast<JoinType>(join),
            PyObject_IsTrue(fill) ? FillType::fill : FillType::noFill)));
    }
    catch (Standard_Failure& e) {
        PyErr_SetString(PartExceptionOCCError, e.GetMessageString());
        return nullptr;
    }
}

PyObject* TopoShapePy::makeOffset2D(PyObject *args, PyObject *keywds) const
{
    static const std::array<const char *, 6> kwlist {"offset", "join", "fill", "openResult", "intersection", nullptr};
    double offset;
    PyObject* fill = Py_False;
    PyObject* openResult = Py_False;
    PyObject* inter = Py_False;
    short join = 0;
    if (!Base::Wrapped_ParseTupleAndKeywords(args, keywds, "d|hO!O!O!", kwlist, &offset, &join,
                                             &(PyBool_Type), &fill, &(PyBool_Type), &openResult, &(PyBool_Type),
                                             &inter)) {
        return nullptr;
    }

    try {
        return Py::new_reference_to(shape2pyshape(getTopoShapePtr()->makeElementOffset2D(
            offset,
            static_cast<JoinType>(join),
            PyObject_IsTrue(fill) ? FillType::fill : FillType::noFill,
            PyObject_IsTrue(openResult) ? OpenResult::allowOpenResult : OpenResult::noOpenResult,
            PyObject_IsTrue(inter) ? true : false)));
    }
    PY_CATCH_OCC;
}

PyObject*  TopoShapePy::reverse(PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;

    TopoDS_Shape shape = getTopoShapePtr()->getShape();
    shape.Reverse();
    getTopoShapePtr()->setShape(shape);

    Py_Return;
}

PyObject*  TopoShapePy::reversed(PyObject *args) const
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;

    TopoDS_Shape shape = getTopoShapePtr()->getShape();
    shape = shape.Reversed();

    PyTypeObject* type = this->GetType();
    PyObject* cpy = nullptr;

    // let the type object decide
    if (type->tp_new)
        cpy = type->tp_new(type, const_cast<TopoShapePy*>(this), nullptr);
    if (!cpy) {
        PyErr_SetString(PyExc_TypeError, "failed to create copy of shape");
        return nullptr;
    }

    if (!shape.IsNull()) {
        static_cast<TopoShapePy*>(cpy)->getTopoShapePtr()->setShape(shape);
    }
    return cpy;
}

PyObject*  TopoShapePy::complement(PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;

    TopoDS_Shape shape = getTopoShapePtr()->getShape();
    shape.Complement();
    getTopoShapePtr()->setShape(shape);

    Py_Return;
}

PyObject*  TopoShapePy::nullify(PyObject *args)
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;

    TopoDS_Shape shape = getTopoShapePtr()->getShape();
    shape.Nullify();
    getTopoShapePtr()->setShape(shape);

    Py_Return;
}

PyObject*  TopoShapePy::isNull(PyObject *args) const
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;

    bool null = getTopoShapePtr()->isNull();
    return Py_BuildValue("O", (null ? Py_True : Py_False));
}

PyObject*  TopoShapePy::isClosed(PyObject *args) const
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;

    try {
        if (getTopoShapePtr()->getShape().IsNull())
            Standard_Failure::Raise("Cannot determine the 'Closed'' flag of an empty shape");
        return Py_BuildValue("O", (getTopoShapePtr()->isClosed() ? Py_True : Py_False));
    }
    catch (...) {
        PyErr_SetString(PyExc_RuntimeError, "check failed, shape may be empty");
        return nullptr;
    }
}

PyObject*  TopoShapePy::isEqual(PyObject *args) const
{
    PyObject *pcObj;
    if (!PyArg_ParseTuple(args, "O!", &(TopoShapePy::Type), &pcObj))
        return nullptr;

    TopoDS_Shape shape = static_cast<TopoShapePy*>(pcObj)->getTopoShapePtr()->getShape();
    Standard_Boolean test = (getTopoShapePtr()->getShape().IsEqual(shape));

    return Py_BuildValue("O", (test ? Py_True : Py_False));
}

PyObject*  TopoShapePy::isSame(PyObject *args) const
{
    PyObject *pcObj;
    if (!PyArg_ParseTuple(args, "O!", &(TopoShapePy::Type), &pcObj))
        return nullptr;

    TopoDS_Shape shape = static_cast<TopoShapePy*>(pcObj)->getTopoShapePtr()->getShape();
    Standard_Boolean test = getTopoShapePtr()->getShape().IsSame(shape);

    return Py_BuildValue("O", (test ? Py_True : Py_False));
}

PyObject*  TopoShapePy::isPartner(PyObject *args) const
{
    PyObject *pcObj;
    if (!PyArg_ParseTuple(args, "O!", &(TopoShapePy::Type), &pcObj))
        return nullptr;

    TopoDS_Shape shape = static_cast<TopoShapePy*>(pcObj)->getTopoShapePtr()->getShape();
    Standard_Boolean test = getTopoShapePtr()->getShape().IsPartner(shape);

    return Py_BuildValue("O", (test ? Py_True : Py_False));
}

PyObject*  TopoShapePy::isValid(PyObject *args) const
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;

    PY_TRY {
        return Py_BuildValue("O", (getTopoShapePtr()->isValid() ? Py_True : Py_False));
    }
    PY_CATCH_OCC
}

PyObject*  TopoShapePy::isCoplanar(PyObject *args) const
{
    PyObject *pyObj;
    double tol = -1;
    if (!PyArg_ParseTuple(args, "O!|d", &TopoShapePy::Type, &pyObj, &tol))
        return nullptr;

    PY_TRY {
        return Py::new_reference_to(Py::Boolean(getTopoShapePtr()->isCoplanar(
                    *static_cast<TopoShapePy*>(pyObj)->getTopoShapePtr(),tol)));
    }
    PY_CATCH_OCC
}

PyObject*  TopoShapePy::isInfinite(PyObject *args) const
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;

    PY_TRY {
        return Py::new_reference_to(Py::Boolean(getTopoShapePtr()->isInfinite()));
    }
    PY_CATCH_OCC
}

PyObject*  TopoShapePy::findPlane(PyObject *args) const
{
    double tol = -1;
    if (!PyArg_ParseTuple(args, "|d", &tol))
        return nullptr;

    PY_TRY {
        gp_Pln pln;
        if (getTopoShapePtr()->findPlane(pln, tol))
            return new PlanePy(new GeomPlane(new Geom_Plane(pln)));
        Py_Return;
    }
    PY_CATCH_OCC
}

PyObject*  TopoShapePy::fix(PyObject *args)
{
    double prec, mintol, maxtol;
    if (!PyArg_ParseTuple(args, "ddd", &prec, &mintol, &maxtol))
        return nullptr;

    try {
        return Py_BuildValue("O", (getTopoShapePtr()->fix(prec, mintol, maxtol) ? Py_True : Py_False));
    }
    catch (...) {
        PyErr_SetString(PyExc_RuntimeError, "check failed, shape may be empty");
        return nullptr;
    }
}

PyObject* TopoShapePy::hashCode(PyObject *args) const
{
    int upper = IntegerLast();
    if (!PyArg_ParseTuple(args, "|i",&upper))
        return nullptr;

    int hc = ShapeMapHasher{}(getTopoShapePtr()->getShape());
    return Py_BuildValue("i", hc);
}

PyObject* TopoShapePy::tessellate(PyObject *args) const
{
    double tolerance;
    PyObject* ok = Py_False;
    if (!PyArg_ParseTuple(args, "d|O!", &tolerance, &PyBool_Type, &ok))
        return nullptr;

    try {
        std::vector<Base::Vector3d> Points;
        std::vector<Data::ComplexGeoData::Facet> Facets;
        if (Base::asBoolean(ok))
            BRepTools::Clean(getTopoShapePtr()->getShape());
        getTopoShapePtr()->getFaces(Points, Facets,tolerance);
        Py::Tuple tuple(2);
        Py::List vertex;
        for (const auto & Point : Points)
            vertex.append(Py::asObject(new Base::VectorPy(Point)));
        tuple.setItem(0, vertex);
        Py::List facet;
        for (const auto& it : Facets) {
            Py::Tuple f(3);
            f.setItem(0,Py::Long((long)it.I1));
            f.setItem(1,Py::Long((long)it.I2));
            f.setItem(2,Py::Long((long)it.I3));
            facet.append(f);
        }
        tuple.setItem(1, facet);
        return Py::new_reference_to(tuple);
    }
    catch (Standard_Failure& e) {
        PyErr_SetString(PartExceptionOCCError, e.GetMessageString());
        return nullptr;
    }
}

PyObject* TopoShapePy::project(PyObject *args) const
{
    PyObject *obj;

    BRepAlgo_NormalProjection algo;
    algo.Init(this->getTopoShapePtr()->getShape());
    if (!PyArg_ParseTuple(args, "O", &obj))
        return nullptr;

    try {
        Py::Sequence list(obj);
        for (Py::Sequence::iterator it = list.begin(); it != list.end(); ++it) {
            if (PyObject_TypeCheck((*it).ptr(), &(Part::TopoShapePy::Type))) {
                const TopoDS_Shape& shape = static_cast<TopoShapePy*>((*it).ptr())->getTopoShapePtr()->getShape();
                algo.Add(shape);
            }
        }

        algo.Compute3d(Standard_True);
        algo.SetLimit(Standard_True);
        algo.SetParams(1.e-6, 1.e-6, GeomAbs_C1, 14, 10000);
        //algo.SetDefaultParams();
        algo.Build();
        return new TopoShapePy(new TopoShape(algo.Projection()));
    }
    catch (Standard_Failure&) {
        PyErr_SetString(PartExceptionOCCError, "Failed to project shape");
        return nullptr;
    }
}

PyObject* TopoShapePy::makeParallelProjection(PyObject *args) const
{
    PyObject *pShape, *pDir;
    if (!PyArg_ParseTuple(args, "O!O!", &(Part::TopoShapePy::Type), &pShape, &Base::VectorPy::Type, &pDir))
        return nullptr;

    try {
        const TopoDS_Shape& shape = this->getTopoShapePtr()->getShape();
        const TopoDS_Shape& wire = static_cast<TopoShapePy*>(pShape)->getTopoShapePtr()->getShape();
        Base::Vector3d vec = Py::Vector(pDir,false).toVector();
        BRepProj_Projection proj(wire, shape, gp_Dir(vec.x,vec.y,vec.z));
        TopoDS_Shape projected = proj.Shape();
        return new TopoShapePy(new TopoShape(projected));
    }
    catch (Standard_Failure& e) {
        PyErr_SetString(PartExceptionOCCError, e.GetMessageString());
        return nullptr;
    }
}

PyObject* TopoShapePy::makePerspectiveProjection(PyObject *args) const
{
    PyObject *pShape, *pDir;
    if (!PyArg_ParseTuple(args, "O!O!", &(Part::TopoShapePy::Type), &pShape, &Base::VectorPy::Type, &pDir))
        return nullptr;

    try {
        const TopoDS_Shape& shape = this->getTopoShapePtr()->getShape();
        const TopoDS_Shape& wire = static_cast<TopoShapePy*>(pShape)->getTopoShapePtr()->getShape();
        Base::Vector3d vec = Py::Vector(pDir,false).toVector();
        BRepProj_Projection proj(wire, shape, gp_Pnt(vec.x,vec.y,vec.z));
        TopoDS_Shape projected = proj.Shape();
        return new TopoShapePy(new TopoShape(projected));
    }
    catch (Standard_Failure& e) {
        PyErr_SetString(PartExceptionOCCError, e.GetMessageString());
        return nullptr;
    }
}

/*!
from pivy import coin

rot=Gui.ActiveDocument.ActiveView.getCameraOrientation()
vdir=App.Vector(0,0,-1)
vdir=rot.multVec(vdir)
udir=App.Vector(0,1,0)
udir=rot.multVec(udir)

pos=Gui.ActiveDocument.ActiveView.getCameraNode().position.getValue().getValue()
pos=App.Vector(*pos)

shape=App.ActiveDocument.ActiveObject.Shape
reflect=shape.reflectLines(ViewDir=vdir, ViewPos=pos, UpDir=udir, EdgeType="Sharp", Visible=True, OnShape=False)
Part.show(reflect)
 */
PyObject* TopoShapePy::reflectLines(PyObject *args, PyObject *kwds) const
{
    static const std::array<const char *, 7> kwlist{"ViewDir", "ViewPos", "UpDir", "EdgeType", "Visible", "OnShape",
                                                    nullptr};

    const char* type="OutLine";
    PyObject* vis = Py_True;
    PyObject* in3d = Py_False;
    PyObject* pPos = nullptr;
    PyObject* pUp = nullptr;
    PyObject *pView;
    if (!Base::Wrapped_ParseTupleAndKeywords(args, kwds, "O!|O!O!sO!O!", kwlist,
                                             &Base::VectorPy::Type, &pView, &Base::VectorPy::Type, &pPos,
                                             &Base::VectorPy::Type,
                                             &pUp, &type, &PyBool_Type, &vis, &PyBool_Type, &in3d)) {
        return nullptr;
    }

    try {
        HLRBRep_TypeOfResultingEdge t;
        std::string str = type;
        if (str == "IsoLine")
            t = HLRBRep_IsoLine;
        else if (str == "Rg1Line")
            t = HLRBRep_Rg1Line;
        else if (str == "RgNLine")
            t = HLRBRep_RgNLine;
        else if (str == "Sharp")
            t = HLRBRep_Sharp;
        else
            t = HLRBRep_OutLine;

        Base::Vector3d p(0.0, 0.0, 0.0);
        if (pPos)
            p = Py::Vector(pPos,false).toVector();
        Base::Vector3d u(0.0, 1.0, 0.0);
        if (pUp)
            u = Py::Vector(pUp,false).toVector();

        Base::Vector3d v = Py::Vector(pView,false).toVector();
        const TopoDS_Shape& shape = this->getTopoShapePtr()->getShape();
        HLRAppli_ReflectLines reflect(shape);
        reflect.SetAxes(v.x, v.y, v.z, p.x, p.y, p.z, u.x, u.y, u.z);
        reflect.Perform();
        TopoDS_Shape lines = reflect.GetCompoundOf3dEdges(t, Base::asBoolean(vis), Base::asBoolean(in3d));
        return new TopoShapePy(new TopoShape(lines));
    }
    catch (Standard_Failure& e) {
        PyErr_SetString(PartExceptionOCCError, e.GetMessageString());
        return nullptr;
    }
}

PyObject* TopoShapePy::makeShapeFromMesh(PyObject *args)
{
    PyObject *tup;
    double tolerance = 1.0e-06;
    PyObject* sewShape = Py_True;
    if (!PyArg_ParseTuple(args, "O!|dO!",&PyTuple_Type, &tup, &tolerance, &PyBool_Type, &sewShape))
        return nullptr;

    try {
        Py::Tuple tuple(tup);
        Py::Sequence vertex(tuple[0]);
        Py::Sequence facets(tuple[1]);

        std::vector<Base::Vector3d> Points;
        for (Py::Sequence::iterator it = vertex.begin(); it != vertex.end(); ++it) {
            Py::Vector vec(*it);
            Points.push_back(vec.toVector());
        }
        std::vector<Data::ComplexGeoData::Facet> Facets;
        for (Py::Sequence::iterator it = facets.begin(); it != facets.end(); ++it) {
            Data::ComplexGeoData::Facet face;
            Py::Tuple f(*it);
            face.I1 = (int)Py::Long(f[0]);
            face.I2 = (int)Py::Long(f[1]);
            face.I3 = (int)Py::Long(f[2]);
            Facets.push_back(face);
        }

        getTopoShapePtr()->setFaces(Points, Facets, tolerance);
        if (Base::asBoolean(sewShape))
            getTopoShapePtr()->sewShape(tolerance);

        Py_Return;
    }
    PY_CATCH_OCC
}

PyObject* TopoShapePy::makeEvolved(PyObject *args, PyObject *kwds) const
{
    PyObject* Profile;
    PyObject* AxeProf = Py_True;
    PyObject* Solid = Py_False;
    PyObject* ProfOnSpine = Py_False;
    auto JoinType = JoinType::arc;
    double Tolerance = 0.0000001;

    static const std::array<const char*, 7> kwds_evolve{"Profile", "Join", "AxeProf", "Solid", "ProfOnSpine", "Tolerance", nullptr};
    if (!Base::Wrapped_ParseTupleAndKeywords(args, kwds, "O!|iO!O!O!d", kwds_evolve,
        &TopoShapePy::Type, &Profile, &JoinType,
        &PyBool_Type, &AxeProf, &PyBool_Type, &Solid,
        &PyBool_Type, &ProfOnSpine, &Tolerance)) {
        return nullptr;
    }
    try {
        return Py::new_reference_to(shape2pyshape(getTopoShapePtr()->makeElementEvolve(
            *static_cast<TopoShapePy*>(Profile)->getTopoShapePtr(), JoinType,
            PyObject_IsTrue(AxeProf) ? CoordinateSystem::global : CoordinateSystem::relativeToSpine,
            PyObject_IsTrue(Solid) ? MakeSolid::makeSolid : MakeSolid::noSolid,
            PyObject_IsTrue(ProfOnSpine) ? Spine::on : Spine::notOn,
            Tolerance)));
    } PY_CATCH_OCC
}

PyObject* TopoShapePy::makeWires(PyObject* args) const
{
    const char *op = nullptr;
    if (!PyArg_ParseTuple(args, "s", &op))
        return nullptr;

    PY_TRY {
        return Py::new_reference_to(shape2pyshape(getTopoShapePtr()->makeWires(op)));
    }
    PY_CATCH_OCC
}

PyObject* TopoShapePy::toNurbs(PyObject *args) const
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;

    try {
        // Convert into nurbs
        TopoDS_Shape nurbs = this->getTopoShapePtr()->toNurbs();
        return new TopoShapePy(new TopoShape(nurbs));
    }
    catch (Standard_Failure& e) {
        PyErr_SetString(PartExceptionOCCError, e.GetMessageString());
        return nullptr;
    }
}

PyObject*  TopoShapePy::isInside(PyObject *args) const
{
    PyObject *point;
    double tolerance;
    PyObject* checkFace = Py_False;
    TopAbs_State stateIn = TopAbs_IN;
    if (!PyArg_ParseTuple(args, "O!dO!", &(Base::VectorPy::Type), &point, &tolerance,  &PyBool_Type, &checkFace))
        return nullptr;

    try {
        TopoDS_Shape shape = getTopoShapePtr()->getShape();
        if (shape.IsNull()) {
            PyErr_SetString(PartExceptionOCCError, "Cannot handle null shape");
            return nullptr;
        }

        Base::Vector3d pnt = static_cast<Base::VectorPy*>(point)->value();
        gp_Pnt vertex = gp_Pnt(pnt.x,pnt.y,pnt.z);
        if (shape.ShapeType() == TopAbs_VERTEX ||
            shape.ShapeType() == TopAbs_EDGE ||
            shape.ShapeType() == TopAbs_WIRE ||
            shape.ShapeType() == TopAbs_FACE) {

            BRepBuilderAPI_MakeVertex mkVertex(vertex);
            BRepExtrema_DistShapeShape extss;
            extss.LoadS1(mkVertex.Vertex());
            extss.LoadS2(shape);
            if (!extss.Perform()) {
                PyErr_SetString(PartExceptionOCCError, "Failed to determine distance to shape");
                return nullptr;
            }
            Standard_Boolean test = (extss.Value() <= tolerance);
            return Py_BuildValue("O", (test ? Py_True : Py_False));
        }
        else {
            BRepClass3d_SolidClassifier solidClassifier(shape);
            solidClassifier.Perform(vertex, tolerance);
            Standard_Boolean test = (solidClassifier.State() == stateIn);

            if (Base::asBoolean(checkFace) && solidClassifier.IsOnAFace())
                test = Standard_True;
            return Py_BuildValue("O", (test ? Py_True : Py_False));
        }
    }
    catch (Standard_Failure& e) {
        PyErr_SetString(PartExceptionOCCError, e.GetMessageString());
        return nullptr;
    }
    catch (const std::exception& e) {
        PyErr_SetString(PartExceptionOCCError, e.what());
        return nullptr;
    }
}

PyObject* TopoShapePy::removeSplitter(PyObject *args) const
{
    if (!PyArg_ParseTuple(args, ""))
        return nullptr;

    try {
        return Py::new_reference_to(shape2pyshape(getTopoShapePtr()->makeElementRefine()));
    }
    catch (Standard_Failure& e) {
        PyErr_SetString(PartExceptionOCCError, e.GetMessageString());
        return nullptr;
    }
}

PyObject* TopoShapePy::getElement(PyObject *args) const
{
    char* input;
    PyObject* silent = Py_False;
    if (!PyArg_ParseTuple(args, "s|O", &input, &silent)) {
        return nullptr;
    }
    try {
        PyObject* res = getTopoShapePtr()->getPySubShape(input, PyObject_IsTrue(silent));
        if (!res) {
            Py_Return;
        }
        return res;
    }
    PY_CATCH_OCC
}

PyObject* TopoShapePy::countElement(PyObject *args) const
{
    char* input;
    if (!PyArg_ParseTuple(args, "s", &input))
        return nullptr;

    PY_TRY {
        return Py::new_reference_to(Py::Long((long)getTopoShapePtr()->countSubShapes(input)));
    }
    PY_CATCH_OCC
}

PyObject* TopoShapePy::getTolerance(PyObject *args) const
{
    int mode;
    PyObject* type = reinterpret_cast<PyObject*>(&TopoShapePy::Type);
    if (!PyArg_ParseTuple(args, "i|O!", &mode, &PyType_Type, &type))
        return nullptr;

    try {
        TopoDS_Shape shape = this->getTopoShapePtr()->getShape();
        PyTypeObject* pyType = reinterpret_cast<PyTypeObject*>(type);
        TopAbs_ShapeEnum shapetype = ShapeTypeFromPyType(pyType);
        if (!PyType_IsSubtype(pyType, &TopoShapePy::Type) ||
            (shapetype != TopAbs_SHAPE && shapetype != TopAbs_VERTEX &&
            shapetype != TopAbs_EDGE && shapetype != TopAbs_FACE && shapetype != TopAbs_SHELL)) {
            PyErr_SetString(PyExc_TypeError, "shape type must be Shape, Vertex, Edge, Face or Shell");
            return nullptr;
        }

        ShapeAnalysis_ShapeTolerance analysis;
        double tolerance = analysis.Tolerance(shape, mode, shapetype);
        return PyFloat_FromDouble(tolerance);
    }
    catch (Standard_Failure& e) {
        PyErr_SetString(PartExceptionOCCError, e.GetMessageString());
        return nullptr;
    }
}

PyObject* TopoShapePy::overTolerance(PyObject *args) const
{
    double value;
    PyObject* type = reinterpret_cast<PyObject*>(&TopoShapePy::Type);
    if (!PyArg_ParseTuple(args, "d|O!", &value, &PyType_Type, &type))
        return nullptr;

    try {
        TopoDS_Shape shape = this->getTopoShapePtr()->getShape();
        PyTypeObject* pyType = reinterpret_cast<PyTypeObject*>(type);
        TopAbs_ShapeEnum shapetype = ShapeTypeFromPyType(pyType);
        if (!PyType_IsSubtype(pyType, &TopoShapePy::Type) ||
            (shapetype != TopAbs_SHAPE && shapetype != TopAbs_VERTEX &&
            shapetype != TopAbs_EDGE && shapetype != TopAbs_FACE && shapetype != TopAbs_SHELL)) {
            PyErr_SetString(PyExc_TypeError, "shape type must be Shape, Vertex, Edge, Face or Shell");
            return nullptr;
        }

        ShapeAnalysis_ShapeTolerance analysis;
        Handle(TopTools_HSequenceOfShape) seq = analysis.OverTolerance(shape, value, shapetype);
        Py::Tuple tuple(seq->Length());
        std::size_t index=0;
        for (int i=1; i <= seq->Length(); i++) {
            TopoDS_Shape item = seq->Value(i);
            tuple.setItem(index++, shape2pyshape(item));
        }
        return Py::new_reference_to(tuple);
    }
    catch (Standard_Failure& e) {
        PyErr_SetString(PartExceptionOCCError, e.GetMessageString());
        return nullptr;
    }
}

PyObject* TopoShapePy::inTolerance(PyObject *args) const
{
    double valmin;
    double valmax;
    PyObject* type = reinterpret_cast<PyObject*>(&TopoShapePy::Type);
    if (!PyArg_ParseTuple(args, "dd|O!", &valmin, &valmax, &PyType_Type, &type))
        return nullptr;

    try {
        TopoDS_Shape shape = this->getTopoShapePtr()->getShape();
        PyTypeObject* pyType = reinterpret_cast<PyTypeObject*>(type);
        TopAbs_ShapeEnum shapetype = ShapeTypeFromPyType(pyType);
        if (!PyType_IsSubtype(pyType, &TopoShapePy::Type) ||
            (shapetype != TopAbs_SHAPE && shapetype != TopAbs_VERTEX &&
            shapetype != TopAbs_EDGE && shapetype != TopAbs_FACE && shapetype != TopAbs_SHELL)) {
            PyErr_SetString(PyExc_TypeError, "shape type must be Shape, Vertex, Edge, Face or Shell");
            return nullptr;
        }

        ShapeAnalysis_ShapeTolerance analysis;
        Handle(TopTools_HSequenceOfShape) seq = analysis.InTolerance(shape, valmin, valmax, shapetype);
        Py::Tuple tuple(seq->Length());
        std::size_t index=0;
        for (int i=1; i <= seq->Length(); i++) {
            TopoDS_Shape item = seq->Value(i);
            tuple.setItem(index++, shape2pyshape(item));
        }
        return Py::new_reference_to(tuple);
    }
    catch (Standard_Failure& e) {
        PyErr_SetString(PartExceptionOCCError, e.GetMessageString());
        return nullptr;
    }
}

PyObject* TopoShapePy::globalTolerance(PyObject *args) const
{
    int mode;
    if (!PyArg_ParseTuple(args, "i", &mode))
        return nullptr;

    try {
        TopoDS_Shape shape = this->getTopoShapePtr()->getShape();
        ShapeAnalysis_ShapeTolerance analysis;
        analysis.Tolerance(shape, mode);
        double tolerance = analysis.GlobalTolerance(mode);

        return PyFloat_FromDouble(tolerance);
    }
    catch (Standard_Failure& e) {
        PyErr_SetString(PartExceptionOCCError, e.GetMessageString());
        return nullptr;
    }
}

PyObject* TopoShapePy::fixTolerance(PyObject *args) const
{
    double value;
    PyObject* type = reinterpret_cast<PyObject*>(&TopoShapePy::Type);
    if (!PyArg_ParseTuple(args, "d|O!", &value, &PyType_Type, &type))
        return nullptr;

    try {
        TopoDS_Shape shape = this->getTopoShapePtr()->getShape();
        PyTypeObject* pyType = reinterpret_cast<PyTypeObject*>(type);
        TopAbs_ShapeEnum shapetype = ShapeTypeFromPyType(pyType);
        if (!PyType_IsSubtype(pyType, &TopoShapePy::Type)) {
            PyErr_SetString(PyExc_TypeError, "type must be a Shape subtype");
            return nullptr;
        }

        ShapeFix_ShapeTolerance fix;
        fix.SetTolerance(shape, value, shapetype);
        Py_Return;
    }
    catch (Standard_Failure& e) {
        PyErr_SetString(PartExceptionOCCError, e.GetMessageString());
        return nullptr;
    }
}

PyObject* TopoShapePy::limitTolerance(PyObject *args) const
{
    double tmin;
    double tmax=0;
    PyObject* type = reinterpret_cast<PyObject*>(&TopoShapePy::Type);
    if (!PyArg_ParseTuple(args, "d|dO!", &tmin, &tmax, &PyType_Type, &type))
        return nullptr;

    try {
        TopoDS_Shape shape = this->getTopoShapePtr()->getShape();
        PyTypeObject* pyType = reinterpret_cast<PyTypeObject*>(type);
        TopAbs_ShapeEnum shapetype = ShapeTypeFromPyType(pyType);
        if (!PyType_IsSubtype(pyType, &TopoShapePy::Type)) {
            PyErr_SetString(PyExc_TypeError, "type must be a Shape subtype");
            return nullptr;
        }

        ShapeFix_ShapeTolerance fix;
        Standard_Boolean ok = fix.LimitTolerance(shape, tmin, tmax, shapetype);
        return PyBool_FromLong(ok ? 1 : 0);
    }
    catch (Standard_Failure& e) {
        PyErr_SetString(PartExceptionOCCError, e.GetMessageString());
        return nullptr;
    }
}

PyObject* _getSupportIndex(const char* suppStr, TopoShape* ts, TopoDS_Shape suppShape) {
    std::stringstream ss;
    TopoDS_Shape subShape;

    unsigned long nSubShapes = ts->countSubShapes(suppStr);
    long supportIndex = -1;
    for (unsigned long j=1; j<=nSubShapes; j++){
        ss.str("");
        ss << suppStr << j;
        subShape = ts->getSubShape(ss.str().c_str());
        if (subShape.IsEqual(suppShape)) {
            supportIndex = j-1;
            break;
        }
    }
    return PyLong_FromLong(supportIndex);
}

PyObject* TopoShapePy::proximity(PyObject *args) const
{
    using BRepExtrema_OverlappedSubShapes = BRepExtrema_MapOfIntegerPackedMapOfInteger;

    PyObject* ps2;
    Standard_Real tol = Precision::Confusion();
    if (!PyArg_ParseTuple(args, "O!|d",&(TopoShapePy::Type), &ps2, &tol))
        return nullptr;

    const TopoDS_Shape& s1 = getTopoShapePtr()->getShape();
    const TopoDS_Shape& s2 = static_cast<Part::TopoShapePy*>(ps2)->getTopoShapePtr()->getShape();
    if (s1.IsNull()) {
        PyErr_SetString(PyExc_ValueError, "proximity: Shape object is invalid");
        return nullptr;
    }
    if (s2.IsNull()) {
        PyErr_SetString(PyExc_ValueError, "proximity: Shape parameter is invalid");
        return nullptr;
    }

    BRepExtrema_ShapeProximity proximity;
    proximity.LoadShape1 (s1);
    proximity.LoadShape2 (s2);
    if (tol > 0.0) {
        proximity.SetTolerance (tol);
    }

    proximity.Perform();
    if (!proximity.IsDone()) {
        PyErr_SetString(PartExceptionOCCError, "BRepExtrema_ShapeProximity failed, make sure the shapes are tessellated");
        return nullptr;
    }

    Py::List overlappssindex1;
    Py::List overlappssindex2;

    for (BRepExtrema_OverlappedSubShapes::Iterator anIt1 (proximity.OverlapSubShapes1()); anIt1.More(); anIt1.Next()) {
        overlappssindex1.append(Py::Long(anIt1.Key() + 1));
    }
    for (BRepExtrema_OverlappedSubShapes::Iterator anIt2 (proximity.OverlapSubShapes2()); anIt2.More(); anIt2.Next()) {
        overlappssindex2.append(Py::Long(anIt2.Key() + 1));
    }

    Py::Tuple tuple(2);
    tuple.setItem(0, overlappssindex1);
    tuple.setItem(1, overlappssindex2);
    return Py::new_reference_to(tuple); //face indexes

}

PyObject* TopoShapePy::distToShape(PyObject *args) const
{
    PyObject* ps2;
    gp_Pnt P1, P2;
    BRepExtrema_SupportType supportType1, supportType2;
    TopoDS_Shape suppS1, suppS2;
    Standard_Real minDist = -1, t1, t2, u1, v1, u2, v2;
    Standard_Real tol = Precision::Confusion();

    if (!PyArg_ParseTuple(args, "O!|d",&(TopoShapePy::Type), &ps2, &tol))
        return nullptr;

    const TopoDS_Shape& s1 = getTopoShapePtr()->getShape();
    TopoShape* ts1 = getTopoShapePtr();
    const TopoDS_Shape& s2 = static_cast<Part::TopoShapePy*>(ps2)->getTopoShapePtr()->getShape();
    TopoShape* ts2 = static_cast<Part::TopoShapePy*>(ps2)->getTopoShapePtr();

    if (s2.IsNull()) {
        PyErr_SetString(PyExc_TypeError, "distToShape: Shape parameter is invalid");
        return nullptr;
    }
    BRepExtrema_DistShapeShape extss;
    extss.SetDeflection(tol);
#if OCC_VERSION_HEX >= 0x070600
    extss.SetMultiThread(true);
#endif
    extss.LoadS1(s1);
    extss.LoadS2(s2);
    try {
        extss.Perform();
    }
    catch (const Standard_Failure& e) {
        PyErr_SetString(PyExc_RuntimeError, e.GetMessageString());
        return nullptr;
    }
    if (!extss.IsDone()) {
        PyErr_SetString(PyExc_RuntimeError, "BRepExtrema_DistShapeShape failed");
        return nullptr;
    }
    Py::List solnPts;
    Py::List solnGeom;
    int count = extss.NbSolution();
    if (count != 0) {
        minDist = extss.Value();
        //extss.Dump(std::cout);
        for (int i=1; i<= count; i++) {
            Py::Object pt1, pt2;
            Py::String suppType1, suppType2;
            Py::Long suppIndex1, suppIndex2;
            Py::Object param1, param2;

            P1 = extss.PointOnShape1(i);
            pt1 = Py::asObject(new Base::VectorPy(new Base::Vector3d(P1.X(), P1.Y(), P1.Z())));
            supportType1 = extss.SupportTypeShape1(i);
            suppS1 = extss.SupportOnShape1(i);
            switch (supportType1) {
                case BRepExtrema_IsVertex:
                    suppType1 = Py::String("Vertex");
                    suppIndex1 = Py::asObject(_getSupportIndex("Vertex", ts1, suppS1));
                    param1 = Py::None();
                    break;
                case BRepExtrema_IsOnEdge:
                    suppType1 = Py::String("Edge");
                    suppIndex1 = Py::asObject(_getSupportIndex("Edge", ts1, suppS1));
                    extss.ParOnEdgeS1(i,t1);
                    param1 = Py::Float(t1);
                    break;
                case BRepExtrema_IsInFace:
                    suppType1 = Py::String("Face");
                    suppIndex1 = Py::asObject(_getSupportIndex("Face", ts1, suppS1));
                    extss.ParOnFaceS1(i,u1,v1);
                    {
                        Py::Tuple tup(2);
                        tup[0] = Py::Float(u1);
                        tup[1] = Py::Float(v1);
                        param1 = tup;
                    }
                    break;
                default:
                    Base::Console().message("distToShape: supportType1 is unknown: %d \n",
                                            static_cast<int>(supportType1));
                    suppType1 = Py::String("Unknown");
                    suppIndex1 = -1;
                    param1 = Py::None();
            }

            P2 = extss.PointOnShape2(i);
            pt2 = Py::asObject(new Base::VectorPy(new Base::Vector3d(P2.X(), P2.Y(), P2.Z())));
            supportType2 = extss.SupportTypeShape2(i);
            suppS2 = extss.SupportOnShape2(i);
            switch (supportType2) {
                case BRepExtrema_IsVertex:
                    suppType2 = Py::String("Vertex");
                    suppIndex2 = Py::asObject(_getSupportIndex("Vertex", ts2, suppS2));
                    param2 = Py::None();
                    break;
                case BRepExtrema_IsOnEdge:
                    suppType2 = Py::String("Edge");
                    suppIndex2 = Py::asObject(_getSupportIndex("Edge", ts2, suppS2));
                    extss.ParOnEdgeS2(i,t2);
                    param2 = Py::Float(t2);
                    break;
                case BRepExtrema_IsInFace:
                    suppType2 = Py::String("Face");
                    suppIndex2 = Py::asObject(_getSupportIndex("Face", ts2, suppS2));
                    extss.ParOnFaceS2(i,u2,v2);
                    {
                        Py::Tuple tup(2);
                        tup[0] = Py::Float(u2);
                        tup[1] = Py::Float(v2);
                        param2 = tup;
                    }
                    break;
                default:
                    Base::Console().message("distToShape: supportType2 is unknown: %d \n",
                                            static_cast<int>(supportType2));
                    suppType2 = Py::String("Unknown");
                    suppIndex2 = -1;
                    param2 = Py::None();
            }
            Py::Tuple pts(2);
            pts[0] = pt1;
            pts[1] = pt2;
            solnPts.append(pts);

            Py::Tuple geom(6);
            geom[0] = suppType1;
            geom[1] = suppIndex1;
            geom[2] = param1;
            geom[3] = suppType2;
            geom[4] = suppIndex2;
            geom[5] = param2;

            solnGeom.append(geom);
        }
    }
    else {
        PyErr_SetString(PyExc_TypeError, "distToShape: No Solutions Found.");
        return nullptr;
    }
    Py::Tuple ret(3);
    ret[0] = Py::Float(minDist);
    ret[1] = solnPts;
    ret[2] = solnGeom;
    return Py::new_reference_to(ret);
}

PyObject* TopoShapePy::optimalBoundingBox(PyObject *args) const
{
    PyObject* useT = Py_True;
    PyObject* useS = Py_False;
    if (!PyArg_ParseTuple(args, "|O!O!", &PyBool_Type, &useT, &PyBool_Type, &useS)) {
        return nullptr;
    }

    try {
        TopoDS_Shape shape = this->getTopoShapePtr()->getShape();
        Bnd_Box bounds;
        BRepBndLib::AddOptimal(shape, bounds,
                               Base::asBoolean(useT),
                               Base::asBoolean(useS));
        bounds.SetGap(0.0);
        Standard_Real xMin, yMin, zMin, xMax, yMax, zMax;
        bounds.Get(xMin, yMin, zMin, xMax, yMax, zMax);

        Base::BoundBox3d box;
        box.MinX = xMin;
        box.MaxX = xMax;
        box.MinY = yMin;
        box.MaxY = yMax;
        box.MinZ = zMin;
        box.MaxZ = zMax;

        Py::BoundingBox pybox(box);
        return Py::new_reference_to(pybox);
    }
    catch (const Standard_Failure& e) {
        throw Py::RuntimeError(e.GetMessageString());
    }
}

PyObject* TopoShapePy::clearCache(PyObject* args)
{
    if (!PyArg_ParseTuple(args, "")) {
        return 0;
    }
    getTopoShapePtr()->initCache(1);
    return IncRef();
}

PyObject* TopoShapePy::defeaturing(PyObject *args) const
{
    PyObject *l;
    if (!PyArg_ParseTuple(args, "O",&l))
        return nullptr;

    try {
        Py::Sequence list(l);
        std::vector<TopoDS_Shape> shapes;
        for (Py::Sequence::iterator it = list.begin(); it != list.end(); ++it) {
            Py::TopoShape sh(*it);
            shapes.push_back(
                sh.extensionObject()->getTopoShapePtr()->getShape()
            );
        }
        PyTypeObject* type = this->GetType();
        PyObject* inst = type->tp_new(type, const_cast<TopoShapePy*>(this), nullptr);
        static_cast<TopoShapePy*>(inst)->getTopoShapePtr()->setShape
            (this->getTopoShapePtr()->defeaturing(shapes));
        return inst;
    }
    catch (const Standard_Failure& e) {
        PyErr_SetString(PartExceptionOCCError, e.GetMessageString());
        return nullptr;
    }
}

PyObject* TopoShapePy::findSubShape(PyObject* args) const
{
    PyObject* pyobj;
    if (!PyArg_ParseTuple(args, "O", &pyobj)) {
        return nullptr;
    }

    PY_TRY
    {
        Py::List res;
        for (auto& s : getPyShapes(pyobj)) {
            int index = getTopoShapePtr()->findShape(s.getShape());
            if (index > 0) {
                res.append(Py::TupleN(Py::String(s.shapeName()), Py::Long(index)));
            }
            else {
                res.append(Py::TupleN(Py::Object(), Py::Long(0)));
            }
        }
        if (PySequence_Check(pyobj)) {
            return Py::new_reference_to(res);
        }
        return Py::new_reference_to(Py::Object(res[0].ptr()));
    }
    PY_CATCH_OCC
}

PyObject* TopoShapePy::findSubShapesWithSharedVertex(PyObject* args, PyObject* keywds) const
{
    static const std::array<const char*, 7> kwlist {"shape", "needName", "checkGeometry", "tol", "atol", "singleResult", nullptr};
    PyObject* pyobj;
    PyObject* needName = Py_False;
    PyObject* checkGeometry = Py_True;
    PyObject* singleResult = Py_False;
    double tol = 1e-7;
    double atol = 1e-12;
    if (!Base::Wrapped_ParseTupleAndKeywords(args,
                                             keywds,
                                             "O!|OOdd",
                                             kwlist,
                                             &Type,
                                             &pyobj,
                                             &needName,
                                             &checkGeometry,
                                             &tol,
                                             &atol,
                                             &singleResult)) {
        return nullptr;
    }

    PY_TRY
    {
        Py::List res;
        const TopoShape& shape = *static_cast<TopoShapePy*>(pyobj)->getTopoShapePtr();
        Data::SearchOptions options;
        if (PyObject_IsTrue(checkGeometry))
            options.setFlag(Data::SearchOption::CheckGeometry);
        if (PyObject_IsTrue(singleResult))
            options.setFlag(Data::SearchOption::SingleResult);
        if (PyObject_IsTrue(needName)) {
            std::vector<std::string> names;
            auto shapes = getTopoShapePtr()->findSubShapesWithSharedVertex(
                shape,
                &names,
                options,
                tol,
                atol);
            for (std::size_t i = 0; i < shapes.size(); ++i) {
                res.append(Py::TupleN(Py::String(names[i]), shape2pyshape(shapes[i])));
            }
        }
        else {
            for (auto& s : getTopoShapePtr()->findSubShapesWithSharedVertex(
                     shape,
                     nullptr,
                     options,
                     tol,
                     atol)) {
                res.append(shape2pyshape(s));
            }
        }
        return Py::new_reference_to(res);
    }
    PY_CATCH_OCC
}

// End of Methods, Start of Attributes

Py::String TopoShapePy::getShapeType() const
{
    TopoDS_Shape sh = getTopoShapePtr()->getShape();
    if (sh.IsNull())
        throw Py::Exception(Base::PyExc_FC_GeneralError, "cannot determine type of null shape");

    TopAbs_ShapeEnum type = sh.ShapeType();
    std::string name;
    switch (type) {
        case TopAbs_COMPOUND:
            name = "Compound";
            break;
        case TopAbs_COMPSOLID:
            name = "CompSolid";
            break;
        case TopAbs_SOLID:
            name = "Solid";
            break;
        case TopAbs_SHELL:
            name = "Shell";
            break;
        case TopAbs_FACE:
            name = "Face";
            break;
        case TopAbs_WIRE:
            name = "Wire";
            break;
        case TopAbs_EDGE:
            name = "Edge";
            break;
        case TopAbs_VERTEX:
            name = "Vertex";
            break;
        case TopAbs_SHAPE:
            name = "Shape";
            break;
    }

    return Py::String(name);
}

Py::String TopoShapePy::getOrientation() const
{
    TopoDS_Shape sh = getTopoShapePtr()->getShape();
    if (sh.IsNull())
        throw Py::Exception(Base::PyExc_FC_GeneralError, "cannot determine orientation of null shape");

    TopAbs_Orientation type = sh.Orientation();
    std::string name;
    switch (type) {
        case TopAbs_FORWARD:
            name = "Forward";
            break;
        case TopAbs_REVERSED:
            name = "Reversed";
            break;
        case TopAbs_INTERNAL:
            name = "Internal";
            break;
        case TopAbs_EXTERNAL:
            name = "External";
            break;
    }

    return Py::String(name);
}

void TopoShapePy::setOrientation(Py::String arg)
{
    TopoDS_Shape sh = getTopoShapePtr()->getShape();
    if (sh.IsNull())
        throw Py::Exception(Base::PyExc_FC_GeneralError, "cannot determine orientation of null shape");

    std::string name = static_cast<std::string>(arg);
    TopAbs_Orientation type;
    if (name == "Forward") {
        type = TopAbs_FORWARD;
    }
    else if (name == "Reversed") {
        type = TopAbs_REVERSED;
    }
    else if (name == "Internal") {
        type = TopAbs_INTERNAL;
    }
    else if (name == "External") {
        type = TopAbs_EXTERNAL;
    }
    else {
        throw Py::AttributeError("Invalid orientation type");
    }

    sh.Orientation(type);
    getTopoShapePtr()->setShape(sh);
}

static Py::List
getElements(const TopoShape& sh, TopAbs_ShapeEnum type, TopAbs_ShapeEnum avoid = TopAbs_SHAPE)
{
    Py::List ret;
    for (auto& shape : sh.getSubTopoShapes(type, avoid)) {
        ret.append(shape2pyshape(shape));
    }
    return ret;
}

PyObject* TopoShapePy::getChildShapes(PyObject* args) const
{
    const char* type;
    const char* avoid = nullptr;
    if (!PyArg_ParseTuple(args, "s|s", &type, &avoid)) {
        return nullptr;
    }

    PY_TRY
    {
        return Py::new_reference_to(
            getElements(*getTopoShapePtr(),
                        TopoShape::shapeType(type),
                        !Base::Tools::isNullOrEmpty(avoid) ? TopoShape::shapeType(avoid) : TopAbs_SHAPE));
    }
    PY_CATCH_OCC;
}

Py::List TopoShapePy::getSubShapes() const
{
    return getElements(*getTopoShapePtr(), TopAbs_SHAPE);
}

Py::List TopoShapePy::getFaces() const
{
    return getElements(*getTopoShapePtr(), TopAbs_FACE);
}

Py::List TopoShapePy::getVertexes() const
{
    return getElements(*getTopoShapePtr(), TopAbs_VERTEX);
}

Py::List TopoShapePy::getShells() const
{
    return getElements(*getTopoShapePtr(), TopAbs_SHELL);
}

Py::List TopoShapePy::getSolids() const
{
    return getElements(*getTopoShapePtr(), TopAbs_SOLID);
}

Py::List TopoShapePy::getCompSolids() const
{
    return getElements(*getTopoShapePtr(), TopAbs_COMPSOLID);
}

Py::List TopoShapePy::getEdges() const
{
    return getElements(*getTopoShapePtr(), TopAbs_EDGE);
}

Py::List TopoShapePy::getWires() const
{
    return getElements(*getTopoShapePtr(), TopAbs_WIRE);
}

Py::List TopoShapePy::getCompounds() const
{
    return getElements(*getTopoShapePtr(), TopAbs_COMPOUND);
}

Py::Float TopoShapePy::getLength() const
{
    const TopoDS_Shape& shape = getTopoShapePtr()->getShape();
    if (shape.IsNull())
        throw Py::RuntimeError("shape is invalid");
    GProp_GProps props;
    BRepGProp::LinearProperties(shape, props);
    return Py::Float(props.Mass());
}

Py::Float TopoShapePy::getArea() const
{
    const TopoDS_Shape& shape = getTopoShapePtr()->getShape();
    if (shape.IsNull())
        throw Py::RuntimeError("shape is invalid");
    GProp_GProps props;
    BRepGProp::SurfaceProperties(shape, props);
    return Py::Float(props.Mass());
}

Py::Float TopoShapePy::getVolume() const
{
    const TopoDS_Shape& shape = getTopoShapePtr()->getShape();
    if (shape.IsNull())
        throw Py::RuntimeError("shape is invalid");
    GProp_GProps props;
    BRepGProp::VolumeProperties(shape, props);
    return Py::Float(props.Mass());
}

PyObject* TopoShapePy::getElementHistory(PyObject* args) const
{
    const char* pyname;
    if (!PyArg_ParseTuple(args, "s", &pyname)) {
        return 0;
    }

    Data::MappedName name(pyname);
    PY_TRY
    {
        Data::MappedName original;
        std::vector<Data::MappedName> history;
        long tag = getTopoShapePtr()->getElementHistory(name, &original, &history);
        if (!tag) {
            Py_Return;
        }
        Py::Tuple ret(3);
        ret.setItem(0, Py::Long(tag));
        std::string tmp;
        ret.setItem(1, Py::String(original.appendToBuffer(tmp)));
        Py::List pyHistory;
        for (auto& h : history) {
            tmp.clear();
            pyHistory.append(Py::String(h.appendToBuffer(tmp)));
        }
        ret.setItem(2, pyHistory);
        return Py::new_reference_to(ret);
    }
    PY_CATCH_OCC
}

struct PyShapeMapper: Part::ShapeMapper
{
    bool populate(MappingStatus status, PyObject* pyobj)
    {
        if (!pyobj || pyobj == Py_None) {
            return true;
        }
        try {
            Py::Sequence seq(pyobj);
            for (size_t i = 0, count = seq.size(); i < count; ++i) {
                Py::Sequence item(seq[i].ptr());
                if (item.size() != 2) {
                    return false;
                }

                Part::ShapeMapper::populate(status,
                                            getPyShapes(item[0].ptr()),
                                            getPyShapes(item[1].ptr()));
            }
        }
        catch (Py::Exception&) {
            PyErr_Clear();
            return false;
        }
        return true;
    }

    void init(PyObject* g, PyObject* m)
    {
        const char* msg =
            "Expect input mapping to be a list of tuple(srcShape|shapeList, dstShape|shapeList)";
        if (!populate(MappingStatus::Generated, g) || !populate(MappingStatus::Modified, m)) {
            throw Py::TypeError(msg);
        }
    }
};

PyObject* TopoShapePy::mapShapes(PyObject* args)
{
    PyObject* generated;
    PyObject* modified;
    const char* op = nullptr;
    if (!PyArg_ParseTuple(args, "OO|s", &generated, &modified, &op)) {
        return 0;
    }
    PY_TRY
    {
        PyShapeMapper mapper;
        mapper.init(generated, modified);
        TopoShape& self = *getTopoShapePtr();
        TopoShape s(self.Tag, self.Hasher);
        s.makeShapeWithElementMap(self.getShape(), mapper, mapper.shapes, op);
        self = s;
        return IncRef();
    }
    PY_CATCH_OCC
}

PyObject* TopoShapePy::mapSubElement(PyObject* args)
{
    const char* op = nullptr;
    PyObject* sh;
    if (!PyArg_ParseTuple(args, "O|s", &sh, &op)) {
        return 0;
    }
    PY_TRY
    {
        getTopoShapePtr()->mapSubElement(getPyShapes(sh), op);
        return IncRef();
    }
    PY_CATCH_OCC
}

PyObject* TopoShapePy::getCustomAttributes(const char* attr) const
{
    if (!attr) {
        return nullptr;
    }
    PY_TRY
    {
        TopoDS_Shape res = getTopoShapePtr()->getSubShape(attr, true);
        if (!res.IsNull()) {
            return Py::new_reference_to(shape2pyshape(res));
        }
    }
    PY_CATCH_OCC
    return nullptr;
}

int TopoShapePy::setCustomAttributes(const char* , PyObject *)
{
    return 0;
}
