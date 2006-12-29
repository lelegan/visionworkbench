#ifndef __VW_MOSAIC_IMAGECOMPOSITE_H__
#define __VW_MOSAIC_IMAGECOMPOSITE_H__

#include <iostream>
#include <vector>
#include <list>

#include <vw/Core/Cache.h>
#include <vw/Math/BBox.h>
#include <vw/Image/ImageView.h>
#include <vw/Image/ImageViewRef.h>
#include <vw/Image/Filter.h>
#include <vw/Image/EdgeExtend.h>
#include <vw/Image/Manipulation.h>
#include <vw/Image/ImageMath.h>
#include <vw/Image/Transform.h>
#include <vw/Image/Algorithms.h>
#include <vw/FileIO/DiskImageResource.h>

namespace vw {
namespace mosaic {

  // *******************************************************************
  // PositionedImage
  // *******************************************************************

  template <class PixelT>
  class PositionedImage : public ImageViewBase<PositionedImage<PixelT> > {
  public:
    int cols_, rows_;
    ImageView<PixelT> image;
    BBox<int,2> bbox;

    template <class ImageT>
    PositionedImage( int cols, int rows, ImageT const& image, BBox<int,2> const& bbox ) : cols_(cols), rows_(rows), image(image), bbox(bbox) {}

    PositionedImage reduce() const {
      const int border = 1;
      int left = std::min( border + (bbox.min().x()+border)%2, bbox.min().x() );
      int top = std::min( border + (bbox.min().y()+border)%2, bbox.min().y() );
      int right = std::min( border + (bbox.width()+left+border)%2, cols_-bbox.min().x()-bbox.width() );
      int bottom = std::min( border + (bbox.height()+top+border)%2, rows_-bbox.min().y()-bbox.height() );
      std::vector<float> kernel(3); kernel[0]=0.25; kernel[1]=0.5; kernel[2]=0.25;
      // I don't quite yet understand why (if?) this is the correct bounding box,
      // but bad things happen without the final "+1"s:
      BBox<int,2> new_bbox( Vector<int,2>( (bbox.min().x()-left)/2, (bbox.min().y()-top)/2 ),
                            Vector<int,2>( (bbox.min().x()-left)/2 + (bbox.width()+left+right+1)/2+1,
                                                (bbox.min().y()-top)/2 + (bbox.height()+top+bottom+1)/2+1 ) );
      // We use vw::rasterize() here rather than ordinary assignment because it is 
      // faster for this particular combination of filtering and subsampling.
      ImageView<PixelT> new_image( new_bbox.width(), new_bbox.height() );
      rasterize( edge_extend( subsample( separable_convolution_filter( edge_extend( image, -left, -top, image.cols()+left+right, image.rows()+top+bottom, ZeroEdgeExtend() ),
                                                                     kernel, kernel, ZeroEdgeExtend() ), 2 ), 0, 0, new_bbox.width(), new_bbox.height(), vw::ConstantEdgeExtend() ),
                 new_image );
      return PositionedImage( (cols_+1)/2, (rows_+1)/2, new_image, new_bbox );
    }
    
    void unpremultiply() {
      image /= select_alpha_channel( image );
    }

    void addto( ImageView<PixelT> const& dest ) const {
      crop( dest, bbox ) += image;
    }

    // Performs additive composition when overlay==false (the default).
    // When overlay==true, it overlays the image on top of the destination,
    // respecting any alpha channel.
    void addto( ImageView<PixelT> const& dest, int ox, int oy, bool overlay = false ) {
      BBox<int,2> sum_bbox = bbox;
      sum_bbox.crop( BBox<int,2>( Vector<int,2>(ox,oy), Vector<int,2>(ox+dest.cols(),oy+dest.rows()) ) );
      if( sum_bbox.empty() ) return;
      if( overlay ) {
        if( PixelHasAlpha<PixelT>::value ) {
          crop( dest, sum_bbox-Vector<int,2>(ox,oy) ) *= 1.0 - select_alpha_channel( crop( image, sum_bbox-bbox.min() ) );
          crop( dest, sum_bbox-Vector<int,2>(ox,oy) ) += crop( image, sum_bbox-bbox.min() );
        }
        else {
          crop( dest, sum_bbox-Vector<int,2>(ox,oy) ) = crop( image, sum_bbox-bbox.min() );
        }
      }
      else {
        crop( dest, sum_bbox-Vector<int,2>(ox,oy) ) += crop( image, sum_bbox-bbox.min() );
      }
    }

    void subtract_expanded( PositionedImage const& other ) {
      rasterize( image - edge_extend( resample( other.image, 2 ), bbox-2*other.bbox.min(), ZeroEdgeExtend() ), image );
    }

    template <class OtherPixT>
    PositionedImage& operator*=( PositionedImage<OtherPixT> const& other ) {
      image *= edge_extend( other.image, bbox-other.bbox.min(), ZeroEdgeExtend() );
      return *this;
    }
    
    int cols() const { return cols_; }
    int rows() const { return rows_; }
    int planes() const { return 1; }
  };


  // *******************************************************************
  // ImageComposite
  // *******************************************************************

  template <class PixelT>
  class ImageComposite : public ImageViewBase<ImageComposite<PixelT> > {
  public:
    typedef PixelT pixel_type;
    typedef typename PixelChannelType<PixelT>::type channel_type;

  private:
    struct Pyramid {
      std::vector<PositionedImage<pixel_type> > images;
      std::vector<PositionedImage<channel_type> > masks;
    };

    class SourceGenerator {
      ImageViewRef<pixel_type> m_source;
    public:
      typedef ImageView<pixel_type> value_type;
      SourceGenerator( ImageViewRef<pixel_type> const& source ) : m_source(source) {}
      size_t size() const {
        return m_source.cols() * m_source.rows() * sizeof(pixel_type);
      }
      boost::shared_ptr<value_type> generate() const {
        return boost::shared_ptr<value_type>( new value_type(m_source) );
      }
    };
    
    class AlphaGenerator {
    public:
      ImageComposite& m_composite;
      int m_index;
    public:
      typedef ImageView<channel_type> value_type;
      AlphaGenerator( ImageComposite& composite, int index ) : m_composite(composite), m_index(index) {}
      size_t size() const {
        return m_composite.sources[m_index].size() / PixelNumChannels<pixel_type>::value;
      }
      boost::shared_ptr<value_type> generate() const { 
        ImageView<pixel_type> source = *m_composite.sources[m_index];
        m_composite.sources[m_index].deprioritize();
        return boost::shared_ptr<value_type>( new value_type( select_alpha_channel( source ) ) );
      }
    };

    class PyramidGenerator {
    public:
      ImageComposite& m_composite;
      int m_index;
    public:
      typedef Pyramid value_type;
      PyramidGenerator( ImageComposite& composite, int index ) : m_composite(composite), m_index(index) {}
      size_t size() const {
        return size_t( m_composite.sources[m_index].size() * 1.66 ); // 1.66 = (5/4)*(4/3)
      }
      boost::shared_ptr<value_type> generate() const;
    };

    friend class PyramidGenerator;

    std::vector<BBox<int,2> > bboxes;
    BBox2i view_bbox, data_bbox;
    int mindim, levels;
    bool m_draft_mode;
    bool m_fill_holes;
    bool m_reuse_masks;
    Cache& m_cache;
    std::vector<ImageViewRef<pixel_type> > sourcerefs;
    std::vector<Cache::Handle<SourceGenerator> > sources;
    std::vector<Cache::Handle<AlphaGenerator> > alphas;
    std::vector<Cache::Handle<PyramidGenerator> > pyramids;

    void generate_masks() const;

    ImageView<pixel_type> blend_patch( BBox<int,2> const& patch_bbox ) const;
    ImageView<pixel_type> draft_patch( BBox<int,2> const& patch_bbox ) const;

  public:
    typedef pixel_type result_type;
    
    ImageComposite() : m_draft_mode(false), m_fill_holes(false), m_reuse_masks(false), m_cache(Cache::system_cache()) {}

    void insert( ImageViewRef<pixel_type> const& image, int x, int y );

    void prepare();
    void prepare(BBox2i const& total_bbox);

    ImageView<pixel_type> generate_patch( BBox<int,2> const& patch_bbox ) const {
      if( m_draft_mode ) return draft_patch( patch_bbox );
      else return blend_patch( patch_bbox );
    }

    void set_draft_mode( bool draft_mode ) { m_draft_mode = draft_mode; }

    void set_fill_holes( bool fill_holes ) { m_fill_holes = fill_holes; }

    void set_reuse_masks( bool reuse_masks ) { m_reuse_masks = reuse_masks; }

    int cols() const {
      return view_bbox.width();
    }

    int rows() const {
      return view_bbox.height();
    }

    BBox2i const& bbox() const {
      return data_bbox;
    }

    int planes() const {
      return 1;
    }

    pixel_type operator()( int x, int y, int p=0 ) const {
      // FIXME: This would be easy to add, though expensive. 
      // What about cacheing output blocks?
      vw_throw( NoImplErr() << "ImageComposite does not support individual pixel access!" );
      return pixel_type(); // never reached
    }

    typedef ProceduralPixelAccessor<ImageComposite> pixel_accessor;
    inline pixel_accessor origin() const { return pixel_accessor( *this, 0, 0 ); }    

    typedef ImageComposite prerasterize_type;
    inline prerasterize_type const& prerasterize( BBox2i const& bbox ) const { return *this; }
    template <class DestT> inline void rasterize( DestT const& dest, BBox2i bbox ) const { dest = generate_patch(bbox); }

  };

} // namespace mosaic
} // namespace vw

template <class PixelT>
void vw::mosaic::ImageComposite<PixelT>::generate_masks() const {
  vw_out(InfoMessage) << "Generating masks..." << std::endl;
  std::vector<boost::shared_ptr<ImageView<channel_type> > > grassfire_im( sources.size() );
  for( unsigned p1=0; p1<sources.size(); ++p1 ) {
    if( ! grassfire_im[p1] ) {
      ImageView<pixel_type> image = *sources[p1];
      grassfire_im[p1].reset( new ImageView<channel_type>( grassfire( select_alpha_channel( image ) ) ) );
    }
    ImageView<channel_type> mask = copy( *grassfire_im[p1] );
    for( unsigned p2=0; p2<sources.size(); ++p2 ) {
      if( p1 == p2 ) continue;
      int ox = bboxes[p2].min().x() - bboxes[p1].min().x();
      int oy = bboxes[p2].max().y() - bboxes[p1].max().y();
      if( ox >= bboxes[p1].width() ||
          oy >= bboxes[p1].height() ||
          -ox >= bboxes[p2].width() ||
          -oy >= bboxes[p2].height() ) {
        grassfire_im[p2].reset();
      }
      else {
        if( ! grassfire_im[p2] ) {
          ImageView<pixel_type> image = *sources[p2];
          grassfire_im[p2].reset( new ImageView<channel_type>( grassfire( select_alpha_channel( image ) ) ) );
        }
        int left = std::max( ox, 0 );
        int top = std::max( oy, 0 );
        int right = std::min( bboxes[p2].width()+ox, bboxes[p1].width() );
        int bottom = std::min( bboxes[p2].height()+oy, bboxes[p1].height() );
        for( int j=top; j<bottom; ++j ) {
          for( int i=left; i<right; ++i ) {
            if( ( (*grassfire_im[p2])(i-ox,j-oy) > mask(i,j) ) ||
                ( (*grassfire_im[p2])(i-ox,j-oy) == mask(i,j) && p2 > p1 ) )
              mask(i,j) = 0;
          }
        }
      }
    }
    mask = threshold( mask );
    std::ostringstream filename;
    filename << "mask." << p1 << ".png";
    write_image( filename.str(), mask );
  }
}


template <class PixelT>
boost::shared_ptr<typename vw::mosaic::ImageComposite<PixelT>::Pyramid> vw::mosaic::ImageComposite<PixelT>::PyramidGenerator::generate() const {
  vw_out(DebugMessage) << "ImageComposite generating pyramid " << m_index << std::endl;
  boost::shared_ptr<Pyramid> ptr( new Pyramid );
  ImageView<pixel_type> source = copy(*m_composite.sources[m_index]);
  m_composite.sources[m_index].deprioritize();

  // This is sort of a kluge: the hole-filling algorithm currently 
  // doesn't cope well with partially-transparent source pixels.
  if( m_composite.m_fill_holes ) source /= select_alpha_channel(source);

  PositionedImage<pixel_type> image_high( m_composite.view_bbox.width(), m_composite.view_bbox.height(), source, m_composite.bboxes[m_index] );
  PositionedImage<pixel_type> image_low = image_high.reduce();
  ImageView<channel_type> mask_image;
  
  std::ostringstream mask_filename;
  mask_filename << "mask." << m_index << ".png";
  read_image( mask_image, mask_filename.str() );
  PositionedImage<channel_type> mask( m_composite.view_bbox.width(), m_composite.view_bbox.height(), mask_image, m_composite.bboxes[m_index] );
  
  for( int l=0; l<m_composite.levels; ++l ) {
    PositionedImage<pixel_type> diff = image_high;
    if( l > 0 ) mask = mask.reduce();
    if( l < m_composite.levels-1 ) {
      PositionedImage<pixel_type> next_image_low = image_low.reduce();
      image_low.unpremultiply();
      diff.subtract_expanded( image_low );
      image_high = image_low;
      image_low = next_image_low;
    }
    diff *= mask;
    ptr->images.push_back( diff );
    ptr->masks.push_back( mask );
  }
  return ptr;
}


template <class PixelT>
void vw::mosaic::ImageComposite<PixelT>::insert( ImageViewRef<pixel_type> const& image, int x, int y ) {
  vw_out(VerboseDebugMessage) << "ImageComposite inserting image " << pyramids.size() << std::endl;
  sourcerefs.push_back( image );
  sources.push_back( m_cache.insert( SourceGenerator( image ) ) );
  alphas.push_back( m_cache.insert( AlphaGenerator( *this, pyramids.size() ) ) );
  pyramids.push_back( m_cache.insert( PyramidGenerator( *this, pyramids.size() ) ) );
  int cols = image.cols(), rows = image.rows();
  BBox<int,2> image_bbox( Vector<int,2>(x, y), Vector<int,2>(x+cols, y+rows) );
  bboxes.push_back( image_bbox );
  if( bboxes.size() == 1 ) {
    view_bbox = bboxes.back();
    data_bbox = bboxes.back();
    mindim = std::min(cols,rows);
  }
  else {
    view_bbox.grow( image_bbox );
    data_bbox.grow( image_bbox );
    mindim = std::min( mindim, std::min(cols,rows) );
  }
}


template <class PixelT>
void vw::mosaic::ImageComposite<PixelT>::prepare() {
  // Translate bboxes to origin
  for( unsigned i=0; i<sources.size(); ++i )
    bboxes[i] -= view_bbox.min();
  data_bbox -= view_bbox.min();

  levels = (int) floorf( log( mindim/2.0 ) / log(2.0) ) - 1;

  if( !m_draft_mode && !m_reuse_masks ) {
    generate_masks();
  }
}

template <class PixelT>
void vw::mosaic::ImageComposite<PixelT>::prepare( BBox2i const& total_bbox ) {
  view_bbox = total_bbox;
  prepare();
}

// Suppose a destination image patch at a given level of the pyramid
// has a bounding box that begins at offset x and has width w.  It
// is affected by a range of pixels at the next level of the pyramid
// starting at x/2 with width (x+w)/2-x/2+1 = (w+x%2)/2+1.  This in
// turn is affected by source image pixels at the current level in
// the range starting at 2*(x/2)-1 = x-x%2-1 with width
// (2*(x+w)/2+1)-(2*(x/2)-1)+1 = w-(x+w)%2+x%2+3.

// Generates a full-resolution patch of the mosaic corresponding
// to the given bounding box.
template <class PixelT>
vw::ImageView<PixelT> vw::mosaic::ImageComposite<PixelT>::blend_patch( BBox<int,2> const& patch_bbox ) const {
  vw_out(DebugMessage) << "ImageComposite compositing patch " << patch_bbox << "..." << std::endl;
  // Compute bboxes and allocate the pyramids
  std::vector<BBox<int,2> > bbox_pyr;
  std::vector<ImageView<pixel_type> > sum_pyr(levels);
  std::vector<ImageView<channel_type> > msum_pyr(levels);
  for( int l=0; l<levels; ++l ) {
    if( l==0 ) bbox_pyr.push_back( patch_bbox );
    else bbox_pyr.push_back( BBox<int,2>( Vector<int,2>( bbox_pyr[l-1].min().x() / 2,
                                                         bbox_pyr[l-1].min().y() / 2 ),
                                          Vector<int,2>( bbox_pyr[l-1].min().x() / 2 + ( bbox_pyr[l-1].width() + bbox_pyr[l-1].min().x() % 2 ) / 2 + 1,
                                                         bbox_pyr[l-1].min().y() / 2 + ( bbox_pyr[l-1].height() + bbox_pyr[l-1].min().y() % 2 ) / 2 + 1) ) );
    sum_pyr[l] = ImageView<pixel_type>( bbox_pyr[l].width(), bbox_pyr[l].height() );
    msum_pyr[l] = ImageView<channel_type>( bbox_pyr[l].width(), bbox_pyr[l].height() );
  }
  
  // Compute the bounding box for source pixels that could 
  // impact the patch.
  BBox<int,2> padded_bbox = patch_bbox;
  for( int l=0; l<levels-1; ++l ) {
    padded_bbox.min().x() = padded_bbox.min().x()/2;
    padded_bbox.min().y() = padded_bbox.min().y()/2;
    padded_bbox.max().x() = padded_bbox.max().x()/2+1;
    padded_bbox.max().y() = padded_bbox.max().y()/2+1;
  }
  for( int l=0; l<levels-1; ++l ) {
    padded_bbox.min().x() = 2*padded_bbox.min().x()-1;
    padded_bbox.min().y() = 2*padded_bbox.min().y()-1;
    padded_bbox.max().x() = 2*padded_bbox.max().x();
    padded_bbox.max().y() = 2*padded_bbox.max().y();
  }
  
  // Make a list of the images whose bounding boxes permit them to
  // impact the patch, prioritizing ones that are already in memory.
  std::list<unsigned> image_list;
  for( unsigned p=0; p<sources.size(); ++p ) {
    if( ! padded_bbox.intersects( bboxes[p] ) ) continue;
    if( ! pyramids[p].valid() ) image_list.push_back( p );
    else image_list.push_front( p );
  }

  // Add each source image pyramid to the blend pyramid.
  std::list<unsigned>::iterator ili=image_list.begin(), ilend=image_list.end();
  for( ; ili!=ilend; ++ili ) {
    unsigned p = *ili;
    boost::shared_ptr<Pyramid> pyr = pyramids[p];
    for( int l=0; l<levels; ++l ) {
      pyr->images[l].addto( sum_pyr[l], bbox_pyr[l].min().x(), bbox_pyr[l].min().y() );
      pyr->masks[l].addto( msum_pyr[l], bbox_pyr[l].min().x(), bbox_pyr[l].min().y() );
    }
  }

  // Collapse the pyramid
  ImageView<pixel_type> composite( sum_pyr[levels-1].cols(), sum_pyr[levels-1].rows() );
  for( int l=levels; l; --l ) {
    if( l < levels ) {
      composite = ImageView<pixel_type>( crop( resample( composite, 2 ), 
                                                      bbox_pyr[l-1].min().x()-2*bbox_pyr[l].min().x(), 
                                                      bbox_pyr[l-1].min().y()-2*bbox_pyr[l].min().y(), 
                                                      sum_pyr[l-1].cols(), sum_pyr[l-1].rows() ) );
    }
    composite += sum_pyr[l-1] / msum_pyr[l-1];
    sum_pyr.pop_back();
    msum_pyr.pop_back();
  }

  if( m_fill_holes ) {
    composite /= select_alpha_channel( composite );
  }
  else {

    // Trim to the maximal source alpha, reloading images if needed
    ImageView<channel_type> alpha( patch_bbox.width(), patch_bbox.height() );
    for( unsigned p=0; p<sources.size(); ++p ) {
      if( ! patch_bbox.intersects( bboxes[p] ) ) continue;
    
      ImageView<channel_type> source_alpha = *alphas[p];
    
      BBox<int,2> overlap = patch_bbox;
      overlap.crop( bboxes[p] );
      for( int j=0; j<overlap.height(); ++j ) {
        for( int i=0; i<overlap.width(); ++i ) {
          if( source_alpha( overlap.min().x()+i-bboxes[p].min().x(), overlap.min().y()+j-bboxes[p].min().y() ) > 
              alpha( overlap.min().x()+i-patch_bbox.min().x(), overlap.min().y()+j-patch_bbox.min().y() ) ) {
            alpha( overlap.min().x()+i-patch_bbox.min().x(), overlap.min().y()+j-patch_bbox.min().y() ) =
              source_alpha( overlap.min().x()+i-bboxes[p].min().x(), overlap.min().y()+j-bboxes[p].min().y() );
          }
        }
      }
    }

    composite *= alpha / select_alpha_channel( composite );
  }

  return composite;
}


// Generates a full-resolution patch of the mosaic corresponding
// to the given bounding box WITHOUT blending.
template <class PixelT>
vw::ImageView<PixelT> vw::mosaic::ImageComposite<PixelT>::draft_patch( BBox<int,2> const& patch_bbox ) const {
  vw_out(DebugMessage) << "ImageComposite compositing patch " << patch_bbox << "..." << std::endl;
  ImageView<pixel_type> composite(patch_bbox.width(),patch_bbox.height());

  // Add each image to the composite.
  for( unsigned p=0; p<sources.size(); ++p ) {
    if( ! patch_bbox.intersects( bboxes[p] ) ) continue;
    BBox2i bbox = patch_bbox;
    bbox.crop( bboxes[p] );
    PositionedImage<pixel_type> image( view_bbox.width(), view_bbox.height(), crop(sourcerefs[p],bbox-bboxes[p].min()), bbox );
    image.addto( composite, patch_bbox.min().x(), patch_bbox.min().y(), true );
  }

  return composite;
}

#endif // __VW_MOSAIC_IMAGECOMPOSITE_H__
