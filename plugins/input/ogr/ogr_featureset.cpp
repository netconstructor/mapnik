/*****************************************************************************
 *
 * This file is part of Mapnik (c++ mapping toolkit)
 *
 * Copyright (C) 2011 Artem Pavlenko
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

// mapnik
#include <mapnik/global.hpp>
#include <mapnik/datasource.hpp>
#include <mapnik/box2d.hpp>
#include <mapnik/geometry.hpp>
#include <mapnik/feature.hpp>
#include <mapnik/feature_layer_desc.hpp>
#include <mapnik/wkb.hpp>
#include <mapnik/unicode.hpp>
#include <mapnik/feature_factory.hpp>

// ogr
#include "ogr_featureset.hpp"
#include "ogr_converter.hpp"
#include "ogr_feature_ptr.hpp"

using mapnik::query;
using mapnik::box2d;
using mapnik::CoordTransform;
using mapnik::Feature;
using mapnik::feature_ptr;
using mapnik::geometry_utils;
using mapnik::transcoder;
using mapnik::feature_factory;


ogr_featureset::ogr_featureset(OGRDataSource & dataset,
                               OGRLayer & layer,
                               OGRGeometry & extent,
                               const std::string& encoding,
                               const bool multiple_geometries)
    : dataset_(dataset),
      layer_(layer),
      layerdef_(layer.GetLayerDefn()),
      tr_(new transcoder(encoding)),
      fidcolumn_(layer_.GetFIDColumn ()),
      multiple_geometries_(multiple_geometries),
      count_(0)
{
    layer_.SetSpatialFilter (&extent);
}

ogr_featureset::ogr_featureset(OGRDataSource & dataset,
                               OGRLayer & layer,
                               const mapnik::box2d<double> & extent,
                               const std::string& encoding,
                               const bool multiple_geometries)
    : dataset_(dataset),
      layer_(layer),
      layerdef_(layer.GetLayerDefn()),
      tr_(new transcoder(encoding)),
      fidcolumn_(layer_.GetFIDColumn()),
      multiple_geometries_(multiple_geometries),
      count_(0)
{
    layer_.SetSpatialFilterRect (extent.minx(),
                                 extent.miny(),
                                 extent.maxx(),
                                 extent.maxy());
}

ogr_featureset::~ogr_featureset()
{
}

feature_ptr ogr_featureset::next()
{
    ogr_feature_ptr feat (layer_.GetNextFeature());

    if ((*feat) != NULL)
    {
        // ogr feature ids start at 0, so add one to stay
        // consistent with other mapnik datasources that start at 1
        const int feature_id = ((*feat)->GetFID() + 1);
        feature_ptr feature(feature_factory::create(feature_id));

        OGRGeometry* geom = (*feat)->GetGeometryRef();
        if (geom && ! geom->IsEmpty())
        {
            ogr_converter::convert_geometry(geom, feature, multiple_geometries_);
        }
#ifdef MAPNIK_DEBUG
        else
        {
            std::clog << "OGR Plugin: feature with null geometry: "
                      << (*feat)->GetFID() << std::endl;
        }
#endif
        ++count_;

        int fld_count = layerdef_->GetFieldCount();
        for (int i = 0; i < fld_count; i++)
        {
            OGRFieldDefn* fld = layerdef_->GetFieldDefn(i);
            const OGRFieldType type_oid = fld->GetType();
            const std::string fld_name = fld->GetNameRef();

            switch (type_oid)
            {
            case OFTInteger:
            {
                boost::put(*feature, fld_name, (*feat)->GetFieldAsInteger(i));
                break;
            }

            case OFTReal:
            {
                boost::put(*feature, fld_name, (*feat)->GetFieldAsDouble(i));
                break;
            }

            case OFTString:
            case OFTWideString:     // deprecated !
            {
                UnicodeString ustr = tr_->transcode((*feat)->GetFieldAsString(i));
                boost::put(*feature, fld_name, ustr);
                break;
            }

            case OFTIntegerList:
            case OFTRealList:
            case OFTStringList:
            case OFTWideStringList: // deprecated !
            {
#ifdef MAPNIK_DEBUG
                std::clog << "OGR Plugin: unhandled type_oid=" << type_oid << std::endl;
#endif
                break;
            }

            case OFTBinary:
            {
#ifdef MAPNIK_DEBUG
                std::clog << "OGR Plugin: unhandled type_oid=" << type_oid << std::endl;
#endif
                //boost::put(*feature,name,feat->GetFieldAsBinary (i, size));
                break;
            }

            case OFTDate:
            case OFTTime:
            case OFTDateTime:       // unhandled !
            {
#ifdef MAPNIK_DEBUG
                std::clog << "OGR Plugin: unhandled type_oid=" << type_oid << std::endl;
#endif
                break;
            }

            default: // unknown
            {
#ifdef MAPNIK_DEBUG
                std::clog << "OGR Plugin: unknown type_oid=" << type_oid << std::endl;
#endif
                break;
            }
            }
        }
        return feature;
    }

#ifdef MAPNIK_DEBUG
    std::clog << "OGR Plugin: " << count_ << " features" << std::endl;
#endif
    return feature_ptr();
}
