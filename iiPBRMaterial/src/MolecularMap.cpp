#include "MolecularMap.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <new>
#include <numbers>
#include <sstream>
#include <utility>

namespace ii::pbr {
namespace {
template<class T> Result<T> fail(ErrorCode code,std::string text) { return std::unexpected(Error{code,std::move(text)}); }
bool finite(MapPoint p) { return std::isfinite(p.x)&&std::isfinite(p.y)&&std::abs(p.x)<=1e7&&std::abs(p.y)<=1e7; }
std::string label(const Atom& a) {
    std::string s;
    if(a.isotopeMassNumber)s="["+std::to_string(a.isotopeMassNumber)+"]";
    s+=a.custom?a.custom->definition().symbol:a.element;
    if(a.implicitHydrogens) { s+='H';if(a.implicitHydrogens>1)s+=std::to_string(a.implicitHydrogens); }
    if(a.formalCharge) { if(std::abs(a.formalCharge)>1)s+=std::to_string(std::abs(a.formalCharge));s+=a.formalCharge>0?'+':'-'; }
    return s;
}
Result<std::vector<MapPoint>> layout(const Molecule& mol,const MapOptions& o) {
    const auto n=mol.atoms().size();
    if(n>256)return fail<std::vector<MapPoint>>(ErrorCode::limit_exceeded,"automatic layout supports at most 256 atoms; provide positions");
    std::vector<std::vector<std::uint32_t>> neighbours(n);
    for(const auto& b:mol.bonds()){neighbours[b.first].push_back(b.second);neighbours[b.second].push_back(b.first);}
    for(auto& row:neighbours)std::sort(row.begin(),row.end());
    std::vector<MapPoint> result(n);std::vector<bool> seen(n,false);
    double nextX=0;
    for(std::uint32_t root=0;root<n;++root) {
        if(seen[root])continue;
        std::vector<std::uint32_t> nodes{root};seen[root]=true;
        for(std::size_t at=0;at<nodes.size();++at)for(auto v:neighbours[nodes[at]])if(!seen[v]){seen[v]=true;nodes.push_back(v);}
        // Simple rings are placed in bond order on a regular polygon.
        bool ring=nodes.size()>=3;
        for(auto id:nodes)ring=ring&&neighbours[id].size()==2;
        if(ring) {
            std::vector<std::uint32_t> ordered{root};auto previous=root;auto current=neighbours[root][0];
            while(current!=root&&ordered.size()<nodes.size()) {
                ordered.push_back(current);const auto& row=neighbours[current];auto next=row[0]==previous?row[1]:row[0];previous=current;current=next;
            }
            if(current==root&&ordered.size()==nodes.size())nodes=std::move(ordered);else ring=false;
        }
        const auto k=nodes.size();
        if(k==1)result[nodes[0]]={0,0};
        else if(k==2){result[nodes[0]]={-o.bondLength/2,0};result[nodes[1]]={o.bondLength/2,0};}
        else {
            const double radius=o.bondLength/(2*std::sin(std::numbers::pi/double(k)));
            for(std::size_t i=0;i<k;++i) {
                const double angle=2*std::numbers::pi*double(i)/double(k)-std::numbers::pi/2;
                result[nodes[i]]={radius*std::cos(angle),radius*std::sin(angle)};
            }
        }
        if(!ring&&k>2) {
            std::vector<MapPoint> force(n);
            for(std::uint32_t iter=0;iter<o.layoutIterations;++iter) {
                std::fill(force.begin(),force.end(),MapPoint{});
                for(std::size_t i=0;i<k;++i)for(std::size_t j=i+1;j<k;++j) {
                    const auto a=nodes[i],b=nodes[j];double dx=result[a].x-result[b].x,dy=result[a].y-result[b].y;
                    double d=std::hypot(dx,dy);
                    if(d<1e-9){dx=(i%2?1.0:-1.0)*1e-6*o.bondLength;dy=1e-6*o.bondLength;d=std::hypot(dx,dy);}
                    const double f=0.055*o.bondLength*o.bondLength/d;
                    force[a].x+=f*dx/d;force[a].y+=f*dy/d;force[b].x-=f*dx/d;force[b].y-=f*dy/d;
                }
                for(auto a:nodes)for(auto b:neighbours[a])if(a<b) {
                    const double dx=result[b].x-result[a].x,dy=result[b].y-result[a].y,d=std::hypot(dx,dy);
                    if(d<1e-9)continue;
                    const double f=0.13*(d-o.bondLength);
                    force[a].x+=f*dx/d;force[a].y+=f*dy/d;force[b].x-=f*dx/d;force[b].y-=f*dy/d;
                }
                const double step=o.bondLength*(0.16*(1-double(iter)/o.layoutIterations)+0.015);
                for(auto id:nodes) {
                    const auto d=std::hypot(force[id].x,force[id].y), scale=d>step?step/d:1.0;
                    result[id].x+=force[id].x*scale;result[id].y+=force[id].y*scale;
                }
            }
        }
        double minX=std::numeric_limits<double>::infinity(),maxX=-minX,minY=minX,maxY=-minX;
        for(auto id:nodes){minX=std::min(minX,result[id].x);maxX=std::max(maxX,result[id].x);minY=std::min(minY,result[id].y);maxY=std::max(maxY,result[id].y);}
        for(auto id:nodes){result[id].x+=nextX-minX;result[id].y-=(minY+maxY)/2;}
        nextX+=(maxX-minX)+2*o.bondLength;
    }
    for(auto p:result)if(!finite(p))return fail<std::vector<MapPoint>>(ErrorCode::limit_exceeded,"automatic layout exceeds the coordinate limit");
    return result;
}
std::string escape(std::string_view s) {
    std::string out;
    for(char c:s) {
        switch(c) {
            case '&':out+="&amp;";break;case '<':out+="&lt;";break;case '>':out+="&gt;";break;
            case '\"':out+="&quot;";break;case '\'':out+="&apos;";break;default:out+=c;
        }
    }
    return out;
}
std::string color(Rgb8 c) {
    constexpr char h[]="0123456789abcdef";std::string s="#000000";
    s[1]=h[c.r>>4];s[2]=h[c.r&15];s[3]=h[c.g>>4];s[4]=h[c.g&15];s[5]=h[c.b>>4];s[6]=h[c.b&15];return s;
}
}
Result<MolecularMap> buildMolecularMap(const Molecule& mol,std::span<const MapPoint> supplied,MapOptions o) {
    if(!std::isfinite(o.bondLength)||!std::isfinite(o.atomRadius)||!std::isfinite(o.bondGap)||!std::isfinite(o.padding)||
       o.bondLength<10||o.bondLength>10000||o.atomRadius<3||o.atomRadius>1000||o.bondGap<=0||o.bondGap>o.atomRadius||
       o.padding<0||o.padding>10000||(supplied.empty()&&(!o.layoutIterations||o.layoutIterations>1000)))
        return fail<MolecularMap>(ErrorCode::invalid_input,"invalid molecular map options");
    if(mol.atoms().empty())return fail<MolecularMap>(ErrorCode::invalid_input,"a molecular map requires atoms");
    if(mol.atoms().size()>10000||mol.bonds().size()>20000)
        return fail<MolecularMap>(ErrorCode::limit_exceeded,"diagram exceeds the atom or bond limit");
    try {
        if(supplied.empty()) {
            std::vector<double> radii; radii.reserve(mol.atoms().size());
            for(const auto& atom:mol.atoms())radii.push_back(std::max(o.atomRadius,4.2*double(label(atom).size())+4));
            for(const auto& bond:mol.bonds())
                if(o.bondLength<=radii[bond.first]+radii[bond.second]+2)
                    return fail<MolecularMap>(ErrorCode::invalid_input,"automatic bond length is too short for bonded atom labels");
        }
        std::vector<MapPoint> points;
        if(supplied.empty()) { auto generated=layout(mol,o);if(!generated)return std::unexpected(generated.error());points=std::move(*generated); }
        else {
            if(supplied.size()!=mol.atoms().size())return fail<MolecularMap>(ErrorCode::invalid_input,"one position is required per atom");
            points.assign(supplied.begin(),supplied.end());
        }
        for(auto p:points)if(!finite(p))return fail<MolecularMap>(ErrorCode::invalid_input,"non-finite or excessive atom coordinates");
        MolecularMap map;map.formula_=mol.formula().canonical;map.atoms_.reserve(points.size());
        for(std::size_t i=0;i<points.size();++i) {
            const auto& atom=mol.atoms()[i];auto text=label(atom);
            const auto radius=std::max(o.atomRadius,4.2*double(text.size())+4);
            map.atoms_.push_back({static_cast<std::uint32_t>(i),points[i],radius,std::move(text),
                atom.custom?atom.custom->definition().symbol:atom.element,atom.custom?atom.custom->definition().id:std::string{}});
        }
        for(std::size_t i=0;i<mol.bonds().size();++i) {
            const auto& bond=mol.bonds()[i];const auto& a=map.atoms_[bond.first];const auto& b=map.atoms_[bond.second];
            const double dx=b.center.x-a.center.x,dy=b.center.y-a.center.y,d=std::hypot(dx,dy);
            if(!std::isfinite(d)||d<=a.radius+b.radius+2)
                return fail<MolecularMap>(ErrorCode::invalid_input,"bonded atom glyphs overlap; supply a less crowded layout");
            const double ux=dx/d,uy=dy/d,px=-uy,py=ux;
            const std::string id=bond.custom?bond.custom->definition().id:std::string{};
            auto line=[&](double offset,bool dashed) {
                map.strokes_.push_back({static_cast<std::uint32_t>(i),
                    {a.center.x+ux*a.radius+px*offset,a.center.y+uy*a.radius+py*offset},
                    {b.center.x-ux*b.radius+px*offset,b.center.y-uy*b.radius+py*offset},dashed,id,false});
            };
            if(bond.stereo!=BondStereo::none) {
                const MapPoint tip{a.center.x+ux*a.radius,a.center.y+uy*a.radius};
                const MapPoint base{b.center.x-ux*b.radius,b.center.y-uy*b.radius};
                const auto half=std::min(o.bondGap*1.5,(d-a.radius-b.radius)/4);
                map.wedges_.push_back({static_cast<std::uint32_t>(i),tip,
                    {base.x+px*half,base.y+py*half},{base.x-px*half,base.y-py*half},
                    bond.stereo==BondStereo::hashedWedge,id});
            } else if(bond.custom) {
                switch(bond.custom->definition().display) {
                case CustomBondDisplay::solid:line(0,false);break;
                case CustomBondDisplay::dashed:line(0,true);break;
                case CustomBondDisplay::doubleLine:line(-o.bondGap/2,false);line(o.bondGap/2,false);break;
                case CustomBondDisplay::tripleLine:line(-o.bondGap,false);line(0,false);line(o.bondGap,false);break;
                }
            } else switch(bond.order) {
                case BondOrder::custom:break;
                case BondOrder::single:line(0,false);break;
                case BondOrder::doubleBond:line(-o.bondGap/2,false);line(o.bondGap/2,false);break;
                case BondOrder::triple:line(-o.bondGap,false);line(0,false);line(o.bondGap,false);break;
                case BondOrder::aromatic:line(-o.bondGap/2,false);line(o.bondGap/2,true);break;
            }
            if(bond.custom&&bond.custom->definition().direction==BondDirection::firstToSecond) {
                const MapPoint tip{b.center.x-ux*b.radius,b.center.y-uy*b.radius};
                const auto length=std::min(10.0,(d-a.radius-b.radius)*0.3);
                for(double side:{-1.0,1.0})map.strokes_.push_back({static_cast<std::uint32_t>(i),
                    {tip.x-ux*length+side*px*length*0.55,tip.y-uy*length+side*py*length*0.55},tip,false,id,true});
            }
        }
        const auto inf=std::numeric_limits<double>::infinity();map.bounds_={inf,inf,-inf,-inf};
        for(const auto& a:map.atoms_) {
            map.bounds_.minX=std::min(map.bounds_.minX,a.center.x-a.radius-o.padding);
            map.bounds_.minY=std::min(map.bounds_.minY,a.center.y-a.radius-o.padding);
            map.bounds_.maxX=std::max(map.bounds_.maxX,a.center.x+a.radius+o.padding);
            map.bounds_.maxY=std::max(map.bounds_.maxY,a.center.y+a.radius+o.padding);
        }
        return map;
    } catch(const std::bad_alloc&) {return fail<MolecularMap>(ErrorCode::limit_exceeded,"molecular map allocation failed");}
}
Result<MolecularMap> buildIsomerMap(const Molecule& molecule,std::size_t index,std::span<const MapPoint> positions,MapOptions options) {
    auto selected=molecule.isomer(index);if(!selected)return std::unexpected(selected.error());
    return buildMolecularMap(*selected,positions,options);
}
Result<void> drawMolecularMap(const MolecularMap& map,MolecularMapSink& sink) {
    try {
        for(const auto& line:map.strokes())if(auto status=sink.stroke(line);!status)return status;
        for(const auto& wedge:map.wedges())if(auto status=sink.wedge(wedge);!status)return status;
        for(const auto& atom:map.atoms())if(auto status=sink.atom(atom);!status)return status;
        return {};
    } catch(const std::exception& e) {return fail<void>(ErrorCode::backend_failure,e.what());}
      catch(...) {return fail<void>(ErrorCode::backend_failure,"molecular map renderer failed");}
}
Result<std::string> molecularMapSvg(const MolecularMap& map,MolecularSvgStyle style) {
    if(!std::isfinite(style.strokeWidth)||style.strokeWidth<=0||style.strokeWidth>50||
       !std::isfinite(style.fontSize)||style.fontSize<4||style.fontSize>200)
        return fail<std::string>(ErrorCode::invalid_input,"invalid SVG style");
    for(const auto& atom:map.atoms())
        if(style.fontSize*0.32*atom.label.size()+2>atom.radius||style.fontSize*0.55+2>atom.radius)
            return fail<std::string>(ErrorCode::invalid_input,"SVG font does not fit the atom glyphs");
    try {
        auto b=map.bounds();const auto margin=style.strokeWidth/2;
        b.minX-=margin;b.minY-=margin;b.maxX+=margin;b.maxY+=margin;
        std::ostringstream out;out.imbue(std::locale::classic());out<<std::fixed<<std::setprecision(3);
        out<<"<svg xmlns=\"http://www.w3.org/2000/svg\" role=\"img\" viewBox=\""<<b.minX<<' '<<b.minY<<' '<<b.maxX-b.minX<<' '<<b.maxY-b.minY<<"\">\n";
        out<<"<title>"<<escape(map.formula())<<" molecular map</title>\n";
        out<<"<rect x=\""<<b.minX<<"\" y=\""<<b.minY<<"\" width=\""<<b.maxX-b.minX<<"\" height=\""<<b.maxY-b.minY<<"\" fill=\""<<color(style.background)<<"\"/>\n";
        for(const auto& s:map.strokes()) {
            out<<"<line data-bond=\""<<s.bondIndex<<"\"";
            if(!s.customBondId.empty())out<<" data-custom-bond=\""<<escape(s.customBondId)<<"\"";
            if(s.arrowHead)out<<" data-bond-arrow=\"true\"";
            out<<" x1=\""<<s.from.x<<"\" y1=\""<<s.from.y<<"\" x2=\""<<s.to.x<<"\" y2=\""<<s.to.y<<"\" stroke=\""<<color(style.line)<<"\" stroke-width=\""<<style.strokeWidth<<"\"";
            if(s.dashed)out<<" stroke-dasharray=\"5 4\"";
            out<<"/>\n";
        }
        for(const auto& w:map.wedges()) {
            if(!w.hashed) {
                out<<"<polygon data-bond=\""<<w.bondIndex<<"\"";
                if(!w.customBondId.empty())out<<" data-custom-bond=\""<<escape(w.customBondId)<<"\"";
                out<<" points=\""<<w.tip.x<<','<<w.tip.y<<' '
                   <<w.left.x<<','<<w.left.y<<' '<<w.right.x<<','<<w.right.y<<"\" fill=\""<<color(style.line)<<"\"/>\n";
            } else {
                for(int i=1;i<=6;++i) {
                    const double t=double(i)/7;
                    const auto ax=w.tip.x+t*(w.left.x-w.tip.x),ay=w.tip.y+t*(w.left.y-w.tip.y);
                    const auto bx=w.tip.x+t*(w.right.x-w.tip.x),by=w.tip.y+t*(w.right.y-w.tip.y);
                    out<<"<line data-bond=\""<<w.bondIndex<<"\"";
                    if(!w.customBondId.empty())out<<" data-custom-bond=\""<<escape(w.customBondId)<<"\"";
                    out<<" x1=\""<<ax<<"\" y1=\""<<ay<<"\" x2=\""<<bx<<"\" y2=\""<<by<<"\" stroke=\""<<color(style.line)<<"\" stroke-width=\""<<style.strokeWidth<<"\"/>\n";
                }
            }
        }
        for(const auto& a:map.atoms()) {
            out<<"<g data-atom=\""<<a.atomIndex<<"\"";
            if(!a.customAtomId.empty())out<<" data-custom-atom=\""<<escape(a.customAtomId)<<"\"";
            out<<"><circle cx=\""<<a.center.x<<"\" cy=\""<<a.center.y<<"\" r=\""<<a.radius<<"\" fill=\""<<color(style.atomFill)<<"\" stroke=\""<<color(style.line)<<"\" stroke-width=\""<<style.strokeWidth<<"\"/>";
            out<<"<text x=\""<<a.center.x<<"\" y=\""<<a.center.y<<"\" text-anchor=\"middle\" dominant-baseline=\"central\" font-family=\"sans-serif\" font-size=\""<<style.fontSize<<"\" fill=\""<<color(style.text)<<"\">"<<escape(a.label)<<"</text></g>\n";
        }
        out<<"</svg>\n";auto result=out.str();
        if(result.size()>16u*1024u*1024u)return fail<std::string>(ErrorCode::limit_exceeded,"SVG exceeds output limit");
        return result;
    } catch(const std::bad_alloc&) {return fail<std::string>(ErrorCode::limit_exceeded,"SVG allocation failed");}
}
}
