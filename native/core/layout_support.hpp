#pragma once
#include "types.hpp"
#include <array>
#include <tuple>
#include <type_traits>
namespace velocitex {
namespace Math {
template<class A,class B> auto Max(A a,B b){using C=std::common_type_t<A,B>;return std::max(C(a),C(b));}
template<class A,class B> auto Min(A a,B b){using C=std::common_type_t<A,B>;return std::min(C(a),C(b));}
template<class A> auto Abs(A a){return std::abs(a);}
inline double Floor(double a){return std::floor(a);} inline double Ceiling(double a){return std::ceil(a);} inline double Sqrt(double a){return std::sqrt(a);}
inline double Round(double a){return std::nearbyint(a);} inline double Round(double a,int){return std::round(a);}
}
namespace MidpointRounding {inline constexpr int AwayFromZero=1;}
template<class T,class V> auto value_or(const std::optional<T>& x,V&& v){if constexpr(std::is_same_v<std::remove_cvref_t<V>,std::optional<T>>)return x?x:std::forward<V>(v);else return x?*x:T(std::forward<V>(v));}
template<class T> T& discard_ref(){thread_local T v{};return v;}
template<class T,class M> std::optional<M> optional_member(const std::optional<T>& v,M T::*m){return v?std::optional<M>((*v).*m):std::nullopt;}
inline bool starts_with(std::u16string_view s,std::u16string_view prefix){return s.starts_with(prefix);}
inline bool starts_with(const std::u16string& s,std::u16string_view prefix){return s.starts_with(prefix);}
inline bool starts_with(const std::optional<std::u16string>& s,std::u16string_view prefix){return s&&s->starts_with(prefix);}
inline bool contains(std::u16string_view s,std::u16string_view part){return s.find(part)!=s.npos;}
inline bool contains(std::u16string_view s,char16_t c){return s.find(c)!=s.npos;}
inline std::u16string trim_start(std::u16string_view s,char16_t c){size_t i=0;while(i<s.size()&&s[i]==c)++i;return std::u16string(s.substr(i));}
inline std::u16string replace(std::u16string s,char16_t a,char16_t b){std::replace(s.begin(),s.end(),a,b);return s;}
inline std::u16string utf16(int cp){std::u16string s;append_scalar(s,cp);return s;}
inline std::u16string to_u16(int n){auto v=std::to_string(n);return {v.begin(),v.end()};}
struct LayoutContext {
 uint8_t Style{},TextSize{6}; int Size{6};double Scale{1};int Metrics{};bool Tight{};
 static constexpr uint8_t Map[]={1,1,1,2,1,1,3,1,1,4,2,1,5,2,1,6,3,1,7,4,2,8,6,3,9,7,6,10,8,7,11,10,9};
 static constexpr double Multipliers[]={.5,.6,.7,.8,.9,1,1.2,1.44,1.728,2.074,2.488};
 LayoutContext(uint8_t style=0,uint8_t size=6):Style(style),TextSize(size){Size=Style<4?TextSize:Map[(TextSize-1)*3+(Style>=6?2:1)];Scale=Multipliers[Size-1];Metrics=Size>=5?0:Size>=3?1:2;Tight=Style>=4;}
 LayoutContext WithStyle(uint8_t style)const{return {style,TextSize};}
 LayoutContext WithSize(uint8_t size)const{return {uint8_t(Style<2?0:(Style&1)|2),size};}
 operator uint8_t()const{return Style;}
 friend bool operator==(const LayoutContext&,const LayoutContext&)=default;
};
}
