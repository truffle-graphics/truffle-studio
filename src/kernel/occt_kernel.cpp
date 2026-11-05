#include "kernel/kernel.hpp"

#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <GProp_GProps.hxx>
#include <Poly_Triangulation.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>

#include <cmath>
#include <numbers>
#include <string>

namespace studio::kernel {

struct Solid::Data {
    TopoDS_Shape shape;
};

const Solid::Data& Solid::data() const {
    if (!data_) { throw KernelError("empty solid"); }
    return *data_;
}

namespace {

constexpr double two_pi = 2 * std::numbers::pi;

struct Frame {
    gp_Pnt origin;
    gp_Dir x;
    gp_Dir y;
    gp_Dir normal;

    gp_Pnt point(Vec2 p) const {
        return origin.Translated(gp_Vec(x) * p.x + gp_Vec(y) * p.y);
    }
    gp_Ax2 axes(Vec2 center) const { return gp_Ax2(point(center), normal, x); }
};

Frame frame(const Plane& plane) {
    const gp_Vec x(plane.x_axis.x, plane.x_axis.y, plane.x_axis.z);
    const gp_Vec y(plane.y_axis.x, plane.y_axis.y, plane.y_axis.z);
    const auto normal = x.Crossed(y);
    if (x.Magnitude() < 1e-12 || normal.Magnitude() < 1e-12) {
        throw KernelError("sketch plane axes are degenerate");
    }
    const gp_Dir nx(x);
    const gp_Dir nn(normal);
    return {gp_Pnt(plane.origin.x, plane.origin.y, plane.origin.z), nx, nn.Crossed(nx), nn};
}

double sweep(const Arc& arc) {
    double value = std::fmod(arc.end_angle - arc.start_angle, two_pi);
    if (value <= 0) { value += two_pi; }
    return value;
}

// Signed area (counter-clockwise positive) of the loop in sketch space.
double signed_area(const Loop& loop) {
    double area = 0;
    const auto add = [&](Vec2 a, Vec2 b) { area += a.x * b.y - b.x * a.y; };
    for (const auto& segment : loop.segments) {
        if (const auto* line = std::get_if<Line>(&segment)) {
            add(line->start, line->end);
        } else if (const auto* arc = std::get_if<Arc>(&segment)) {
            constexpr int steps = 32;
            const double span = sweep(*arc);
            const double before = area;
            Vec2 previous{arc->center.x + arc->radius * std::cos(arc->start_angle),
                          arc->center.y + arc->radius * std::sin(arc->start_angle)};
            for (int i = 1; i <= steps; ++i) {
                const double angle = arc->start_angle + span * i / steps;
                const Vec2 next{arc->center.x + arc->radius * std::cos(angle),
                                arc->center.y + arc->radius * std::sin(angle)};
                add(previous, next);
                previous = next;
            }
            if (arc->reversed) { area = before - (area - before); }
        } else {
            const auto& circle = std::get<Circle>(segment);
            area += 2 * std::numbers::pi * circle.radius * circle.radius;
        }
    }
    return area / 2;
}

TopoDS_Wire make_wire(const Frame& f, const Loop& loop) {
    if (loop.segments.empty()) { throw KernelError("a profile loop has no segments"); }
    BRepBuilderAPI_MakeWire wire;
    for (const auto& segment : loop.segments) {
        TopoDS_Edge edge;
        if (const auto* line = std::get_if<Line>(&segment)) {
            BRepBuilderAPI_MakeEdge make(f.point(line->start), f.point(line->end));
            if (!make.IsDone()) { throw KernelError("a line segment is degenerate"); }
            edge = make.Edge();
        } else if (const auto* arc = std::get_if<Arc>(&segment)) {
            if (arc->radius <= 0) { throw KernelError("an arc has no radius"); }
            const gp_Circ circle(f.axes(arc->center), arc->radius);
            const double start = arc->start_angle;
            GC_MakeArcOfCircle make(circle, start, start + sweep(*arc), true);
            if (!make.IsDone()) { throw KernelError("an arc segment is degenerate"); }
            edge = BRepBuilderAPI_MakeEdge(make.Value()).Edge();
            if (arc->reversed) { edge.Reverse(); }
        } else {
            const auto& circle = std::get<Circle>(segment);
            if (circle.radius <= 0) { throw KernelError("a circle has no radius"); }
            edge = BRepBuilderAPI_MakeEdge(gp_Circ(f.axes(circle.center), circle.radius)).Edge();
        }
        wire.Add(edge);
        if (!wire.IsDone()) { throw KernelError("profile segments are not connected"); }
    }
    if (!wire.Wire().Closed()) { throw KernelError("a profile loop is not closed"); }
    return wire.Wire();
}

TopoDS_Face make_face(const Frame& f, const Profile& profile) {
    auto outer = make_wire(f, profile.outer);
    const bool outer_ccw = signed_area(profile.outer) > 0;
    if (!outer_ccw) { outer = TopoDS::Wire(outer.Reversed()); }
    const gp_Pln plane(gp_Ax3(f.origin, f.normal, f.x));
    BRepBuilderAPI_MakeFace face(plane, outer, true);
    if (!face.IsDone()) { throw KernelError("the profile does not form a face"); }
    for (const auto& hole : profile.holes) {
        auto wire = make_wire(f, hole);
        if (signed_area(hole) > 0) { wire = TopoDS::Wire(wire.Reversed()); }
        face.Add(wire);
    }
    if (!face.IsDone()) { throw KernelError("a profile hole is invalid"); }
    return face.Face();
}

const TopoDS_Shape& shape(const Solid& solid) { return solid.data().shape; }

template <typename Body> auto guarded(const char* operation, Body&& body) {
    try {
        return body();
    } catch (const Standard_Failure& failure) {
        throw KernelError(std::string(operation) + " failed: " + failure.what());
    }
}

} // namespace

Solid extrude(const Plane& plane, std::span<const Profile> profiles, double distance) {
    return guarded("extrude", [&] {
        if (profiles.empty()) { throw KernelError("nothing to extrude"); }
        if (std::abs(distance) < 1e-9) { throw KernelError("extrude distance is zero"); }
        const auto f = frame(plane);
        const gp_Vec direction = gp_Vec(f.normal) * distance;
        TopoDS_Shape result;
        for (const auto& profile : profiles) {
            BRepPrimAPI_MakePrism prism(make_face(f, profile), direction);
            if (!prism.IsDone()) { throw KernelError("the prism could not be built"); }
            if (result.IsNull()) {
                result = prism.Shape();
                continue;
            }
            BRepAlgoAPI_Fuse fuse(result, prism.Shape());
            if (!fuse.IsDone() || fuse.HasErrors()) { throw KernelError("profiles could not be united"); }
            result = fuse.Shape();
        }
        return Solid(std::make_shared<const Solid::Data>(Solid::Data{result}));
    });
}

Mesh tessellate(const Solid& solid, double tolerance) {
    return guarded("tessellate", [&] {
        const auto& body = shape(solid);
        BRepMesh_IncrementalMesh mesher(body, tolerance, false, 0.35, true);
        if (!mesher.IsDone()) { throw KernelError("meshing failed"); }
        Mesh mesh;
        for (TopExp_Explorer faces(body, TopAbs_FACE); faces.More(); faces.Next()) {
            const auto& face = TopoDS::Face(faces.Current());
            TopLoc_Location location;
            const auto triangulation = BRep_Tool::Triangulation(face, location);
            if (triangulation.IsNull()) { continue; }
            if (!triangulation->HasNormals()) { triangulation->ComputeNormals(); }
            const bool reversed = face.Orientation() == TopAbs_REVERSED;
            const auto& transform = location.Transformation();
            const auto base = static_cast<std::uint32_t>(mesh.positions.size() / 3);
            for (int i = 1; i <= triangulation->NbNodes(); ++i) {
                const auto point = triangulation->Node(i).Transformed(transform);
                gp_Dir normal = triangulation->Normal(i);
                normal.Transform(transform);
                if (reversed) { normal.Reverse(); }
                mesh.positions.insert(mesh.positions.end(), {static_cast<float>(point.X()),
                                      static_cast<float>(point.Y()), static_cast<float>(point.Z())});
                mesh.normals.insert(mesh.normals.end(), {static_cast<float>(normal.X()),
                                    static_cast<float>(normal.Y()), static_cast<float>(normal.Z())});
            }
            for (int i = 1; i <= triangulation->NbTriangles(); ++i) {
                int a = 0;
                int b = 0;
                int c = 0;
                triangulation->Triangle(i).Get(a, b, c);
                if (reversed) { std::swap(b, c); }
                for (const auto index : {a, b, c}) {
                    mesh.indices.push_back(base + static_cast<std::uint32_t>(index - 1));
                }
            }
        }
        return mesh;
    });
}

double volume(const Solid& solid) {
    return guarded("volume", [&] {
        GProp_GProps properties;
        BRepGProp::VolumeProperties(shape(solid), properties);
        return properties.Mass();
    });
}

Bounds bounds(const Solid& solid) {
    return guarded("bounds", [&] {
        Bnd_Box box;
        BRepBndLib::AddOptimal(shape(solid), box, false, false);
        if (box.IsVoid()) { throw KernelError("the solid has no extent"); }
        double x0 = 0, y0 = 0, z0 = 0, x1 = 0, y1 = 0, z1 = 0;
        box.Get(x0, y0, z0, x1, y1, z1);
        return Bounds{{x0, y0, z0}, {x1, y1, z1}};
    });
}

} // namespace studio::kernel
