// Smoke tests for the prebuilt OpenCASCADE libraries (roadmap milestone M0.2).
//
// They use OCCT directly on purpose: they check that the kernel builds, links and behaves the
// same on every platform. Product code goes through the kernel adapter instead.

#include <BRepAdaptor_Curve.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <GProp_GProps.hxx>
#include <Message.hxx>
#include <Message_PrinterOStream.hxx>
#include <STEPControl_Reader.hxx>
#include <STEPControl_Writer.hxx>
#include <Standard_Failure.hxx>
#include <Standard_Version.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Shape.hxx>
#include <doctest.h>

#include <numbers>
#include <sstream>
#include <string>

namespace {

// OCCT prints STEP transfer statistics through its default messenger; keep test output readable.
[[maybe_unused]] const bool kOcctQuiet = [] {
    Message::DefaultMessenger()->RemovePrinters(STANDARD_TYPE(Message_PrinterOStream));
    return true;
}();

double volume(const TopoDS_Shape& shape) {
    GProp_GProps props;
    BRepGProp::VolumeProperties(shape, props);
    return props.Mass();
}

// First edge of the shape that runs parallel to the Z axis.
TopoDS_Edge firstVerticalEdge(const TopoDS_Shape& shape) {
    for (TopExp_Explorer it(shape, TopAbs_EDGE); it.More(); it.Next()) {
        const TopoDS_Edge& edge = TopoDS::Edge(it.Current());
        BRepAdaptor_Curve curve(edge);
        if (curve.GetType() == GeomAbs_Line &&
            curve.Line().Direction().IsParallel(gp::DZ(), 1e-9)) {
            return edge;
        }
    }
    return {};
}

}  // namespace

TEST_CASE("OpenCASCADE version matches the pinned version") {
    CHECK(std::string(OCC_VERSION_COMPLETE) == FACETRY_EXPECTED_OCCT_VERSION);
}

TEST_CASE("filleting one edge of a box removes the expected volume") {
    const double dx = 20.0;
    const double dy = 10.0;
    const double dz = 5.0;
    const double radius = 2.0;
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(dx, dy, dz).Shape();
    REQUIRE(volume(box) == doctest::Approx(dx * dy * dz));

    const TopoDS_Edge edge = firstVerticalEdge(box);
    REQUIRE_FALSE(edge.IsNull());

    BRepFilletAPI_MakeFillet fillet(box);
    fillet.Add(radius, edge);
    fillet.Build();
    REQUIRE(fillet.IsDone());

    // A fillet on a straight edge of length dz removes (1 - pi/4) * r^2 * dz.
    const double removed = (1.0 - std::numbers::pi / 4.0) * radius * radius * dz;
    CHECK(volume(fillet.Shape()) == doctest::Approx(dx * dy * dz - removed).epsilon(1e-9));
}

TEST_CASE("kernel failures surface as C++ exceptions") {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(20.0, 10.0, 5.0).Shape();
    BRepFilletAPI_MakeFillet fillet(box);
    fillet.Add(50.0, firstVerticalEdge(box));  // radius far larger than the box

    // Depending on the case, OCCT either throws from Build() or reports "not done".
    bool failed = false;
    try {
        fillet.Build();
        failed = !fillet.IsDone();
    } catch (const Standard_Failure&) {
        failed = true;
    }
    CHECK(failed);
    CHECK_THROWS_AS(static_cast<void>(fillet.Shape()), Standard_Failure);
}

TEST_CASE("a box survives a STEP round trip in memory") {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(20.0, 10.0, 5.0).Shape();

    STEPControl_Writer writer;
    REQUIRE(writer.Transfer(box, STEPControl_AsIs) == IFSelect_RetDone);
    std::ostringstream out;
    REQUIRE(writer.WriteStream(out) == IFSelect_RetDone);

    std::istringstream in(out.str());
    STEPControl_Reader reader;
    REQUIRE(reader.ReadStream("box.step", in) == IFSelect_RetDone);
    REQUIRE(reader.TransferRoots() > 0);
    CHECK(volume(reader.OneShape()) == doctest::Approx(1000.0));
}
