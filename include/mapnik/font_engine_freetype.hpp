/*****************************************************************************
 *
 * This file is part of Mapnik (c++ mapping toolkit)
 *
 * Copyright (C) 2006 Artem Pavlenko
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 *****************************************************************************/

//$Id$

#ifndef FONT_ENGINE_FREETYPE_HPP
#define FONT_ENGINE_FREETYPE_HPP
// mapnik
#include <mapnik/color.hpp>
#include <mapnik/utils.hpp>
#include <mapnik/ctrans.hpp>
#include <mapnik/geometry.hpp>
#include <mapnik/text_path.hpp>
#include <mapnik/font_set.hpp>

// freetype2
extern "C"
{
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_STROKER_H
}

// boost
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/utility.hpp>
#include <boost/ptr_container/ptr_vector.hpp>
#ifdef MAPNIK_THREADSAFE
#include <boost/thread/mutex.hpp>
#endif


// stl
#include <string>
#include <vector>
#include <map>
#include <iostream>
#include <algorithm>

// icu
#include <unicode/ubidi.h>
#include <unicode/ushape.h>

namespace mapnik
{
class font_face;

typedef boost::shared_ptr<font_face> face_ptr;

class MAPNIK_DECL font_glyph : private boost::noncopyable
{
public:
    font_glyph(face_ptr face, unsigned index)
        : face_(face), index_(index) {}

    face_ptr get_face() const
    {
        return face_;
    }

    unsigned get_index() const
    {
        return index_;
    }
private:
    face_ptr face_;
    unsigned index_;
};

typedef boost::shared_ptr<font_glyph> glyph_ptr;

class font_face : boost::noncopyable
{
public:
    font_face(FT_Face face)
        : face_(face) {}

    std::string  family_name() const
    {
        return std::string(face_->family_name);
    }

    std::string  style_name() const
    {
        return std::string(face_->style_name);
    }

    FT_GlyphSlot glyph() const
    {
        return face_->glyph;
    }

    FT_Face get_face() const
    {
        return face_;
    }

    unsigned get_char(unsigned c) const
    {
        return FT_Get_Char_Index(face_, c);
    }

    bool set_pixel_sizes(unsigned size)
    {
        if (! FT_Set_Pixel_Sizes( face_, 0, size ))
            return true;

        return false;
    }

    ~font_face()
    {
#ifdef MAPNIK_DEBUG
        std::clog << "~font_face: Clean up face \"" << family_name()
                  << " " << style_name() << "\"" << std::endl;
#endif
        FT_Done_Face(face_);
    }

private:
    FT_Face face_;
};

class MAPNIK_DECL font_face_set : private boost::noncopyable
{
public:
    class dimension_t {
    public:
        dimension_t(double width_, double ymax_, double ymin_, double linespacing_) :  width(width_), height(ymax_-ymin_), linespacing(linespacing_), ymin(ymin_) {}
        double width, height;
        double linespacing;
        double ymin;
    };

    font_face_set(void)
        : faces_() {}

    void add(face_ptr face)
    {
        faces_.push_back(face);
        dimension_cache_.clear(); //Make sure we don't use old cached data
    }

    unsigned size() const
    {
        return faces_.size();
    }

    glyph_ptr get_glyph(unsigned c) const
    {
        for (std::vector<face_ptr>::const_iterator face = faces_.begin(); face != faces_.end(); ++face)
        {
            FT_UInt g = (*face)->get_char(c);

            if (g) return boost::make_shared<font_glyph>(*face, g);
        }

        // Final fallback to empty square if nothing better in any font
        return boost::make_shared<font_glyph>(*faces_.begin(), 0);
    }

    dimension_t character_dimensions(const unsigned c);

    void get_string_info(string_info& info, UnicodeString const& ustr, char_properties *format = 0);

    void set_pixel_sizes(unsigned size)
    {
        for (std::vector<face_ptr>::iterator face = faces_.begin(); face != faces_.end(); ++face)
        {
            (*face)->set_pixel_sizes(size);
        }
    }
private:
    std::vector<face_ptr> faces_;
    std::map<unsigned, dimension_t> dimension_cache_;
};

// FT_Stroker wrapper
class stroker : boost::noncopyable
{
public:
    explicit stroker(FT_Stroker s)
        : s_(s) {}
    
    void init(double radius)
    {
        FT_Stroker_Set(s_,radius * (1<<6), 
                       FT_STROKER_LINECAP_ROUND, 
                       FT_STROKER_LINEJOIN_ROUND, 
                       0);    
    }
    
    FT_Stroker const& get() const
    {
        return s_;
    }
    
    ~stroker()
    {
#ifdef MAPNIK_DEBUG
        std::clog << "~stroker: destroy stroker:" << s_ << std::endl;
#endif        
        FT_Stroker_Done(s_);
    }
private:
    FT_Stroker s_;
};



typedef boost::shared_ptr<font_face_set> face_set_ptr;
typedef boost::shared_ptr<stroker> stroker_ptr;

class MAPNIK_DECL freetype_engine
{
public:
    static bool is_font_file(std::string const& file_name);
    static bool register_font(std::string const& file_name);
    static bool register_fonts(std::string const& dir, bool recurse = false);
    static std::vector<std::string> face_names();
    static std::map<std::string,std::string> const& get_mapping();
    face_ptr create_face(std::string const& family_name);
    stroker_ptr create_stroker();
    virtual ~freetype_engine();
    freetype_engine();
private:
    FT_Library library_;
#ifdef MAPNIK_THREADSAFE
    static boost::mutex mutex_;
#endif
    static std::map<std::string,std::string> name2file_;
};

template <typename T>
class MAPNIK_DECL face_manager : private boost::noncopyable
{
    typedef T font_engine_type;
    typedef std::map<std::string,face_ptr> faces;

public:
    face_manager(T & engine)
        : engine_(engine),
        stroker_(engine_.create_stroker())  {}

    face_ptr get_face(std::string const& name)
    {
        typename faces::iterator itr;
        itr = faces_.find(name);
        if (itr != faces_.end())
        {
            return itr->second;
        }
        else
        {
            face_ptr face = engine_.create_face(name);
            if (face)
            {
                faces_.insert(make_pair(name,face));
            }
            return face;
        }
    }

    face_set_ptr get_face_set(std::string const& name)
    {
        face_set_ptr face_set = boost::make_shared<font_face_set>();
        if (face_ptr face = get_face(name))
        {
            face_set->add(face);
        }
        return face_set;
    }

    face_set_ptr get_face_set(font_set const& fset)
    {
        std::vector<std::string> const& names = fset.get_face_names();
        face_set_ptr face_set = boost::make_shared<font_face_set>();
        for (std::vector<std::string>::const_iterator name = names.begin(); name != names.end(); ++name)
        {
            if (face_ptr face = get_face(*name))
            {
                face_set->add(face);
            }
        }
        return face_set;
    }

    face_set_ptr get_face_set(std::string const& name, font_set const& fset)
    {
        if (fset.size() > 0)
        {
            return get_face_set(fset);
        }
        else
        {
            return get_face_set(name);
        }
    }

    stroker_ptr get_stroker()
    {
        return stroker_;
    }

private:
    faces faces_;
    font_engine_type & engine_;
    stroker_ptr stroker_;
};

template <typename T>
struct text_renderer : private boost::noncopyable
{
    struct glyph_t : boost::noncopyable
    {
        FT_Glyph image;
        char_properties *properties;
        glyph_t(FT_Glyph image_, char_properties *properties_) : image(image_), properties(properties_) {}
        ~glyph_t () { FT_Done_Glyph(image);}
    };

    typedef boost::ptr_vector<glyph_t> glyphs_t;
    typedef T pixmap_type;

    text_renderer (pixmap_type & pixmap, face_manager<freetype_engine> &font_manager_, stroker & s);

    box2d<double> prepare_glyphs(text_path *path);

    void render(double x0, double y0);
    void render_id(int feature_id,double x0, double y0, double min_radius=1.0);
    
private:

    void render_bitmap(FT_Bitmap *bitmap, unsigned rgba, int x, int y, double opacity)
    {
        int x_max=x+bitmap->width;
        int y_max=y+bitmap->rows;
        int i,p,j,q;

        for (i=x,p=0;i<x_max;++i,++p)
        {
            for (j=y,q=0;j<y_max;++j,++q)
            {
                int gray=bitmap->buffer[q*bitmap->width+p];
                if (gray)
                {
                    pixmap_.blendPixel2(i, j, rgba, gray, opacity);
                }
            }
        }
    }

    void render_bitmap_id(FT_Bitmap *bitmap,int feature_id,int x,int y)
    {
        int x_max=x+bitmap->width;
        int y_max=y+bitmap->rows;
        int i,p,j,q;

        for (i=x,p=0;i<x_max;++i,++p)
        {
            for (j=y,q=0;j<y_max;++j,++q)
            {
                int gray=bitmap->buffer[q*bitmap->width+p];
                if (gray)
                {
                    pixmap_.setPixel(i,j,feature_id);
                    //pixmap_.blendPixel2(i,j,rgba,gray,opacity_);
                }
            }
        }
    }

    pixmap_type & pixmap_;
    face_manager<freetype_engine> &font_manager_;
    stroker & stroker_;
    glyphs_t glyphs_;
};
typedef face_manager<freetype_engine> face_manager_freetype;
}

#endif // FONT_ENGINE_FREETYPE_HPP
