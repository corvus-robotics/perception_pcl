#ifndef pcl_ROS_POINT_CLOUD_H_
#define pcl_ROS_POINT_CLOUD_H_

#include <ros/ros.h>
#include <pcl/point_cloud.h>
#include <pcl/point_traits.h>
#include <pcl/for_each_type.h>
#include <pcl/conversions.h>
#include <pcl_conversions/pcl_conversions.h>
#include <sensor_msgs/PointCloud2.h>
#include <boost/mpl/size.hpp>
#include <boost/ref.hpp>
#include <boost/thread/mutex.hpp>

namespace pcl 
{
  namespace detail 
  {
    template<typename Stream, typename PointT>
    struct FieldStreamer
    {
      FieldStreamer(Stream& stream) : stream_(stream) {}

      template<typename U> void operator() ()
      {
        const char* name = traits::name<PointT, U>::value;
        std::uint32_t name_length = strlen(name);
        stream_.next(name_length);
        if (name_length > 0)
          memcpy(stream_.advance(name_length), name, name_length);

        std::uint32_t offset = traits::offset<PointT, U>::value;
        stream_.next(offset);

        std::uint8_t datatype = traits::datatype<PointT, U>::value;
        stream_.next(datatype);

        std::uint32_t count = traits::datatype<PointT, U>::size;
        stream_.next(count);
      }

      Stream& stream_;
    };

    template<typename PointT>
    struct FieldsLength
    {
      FieldsLength() : length(0) {}

      template<typename U> void operator() ()
      {
        std::uint32_t name_length = strlen(traits::name<PointT, U>::value);
        length += name_length + 13;
      }

      std::uint32_t length;
    };
  } // namespace pcl::detail
} // namespace pcl

namespace ros 
{
  namespace message_traits 
  {
    template<typename T> struct MD5Sum<pcl::PointCloud<T> >
    {
      static const char* value() { return MD5Sum<sensor_msgs::PointCloud2>::value(); }
      static const char* value(const pcl::PointCloud<T>&) { return value(); }

      static const uint64_t static_value1 = MD5Sum<sensor_msgs::PointCloud2>::static_value1;
      static const uint64_t static_value2 = MD5Sum<sensor_msgs::PointCloud2>::static_value2;
      
      // If the definition of sensor_msgs/PointCloud2 changes, we'll get a compile error here.
      ROS_STATIC_ASSERT(static_value1 == 0x1158d486dd51d683ULL);
      ROS_STATIC_ASSERT(static_value2 == 0xce2f1be655c3c181ULL);
    };

    template<typename T> struct DataType<pcl::PointCloud<T> >
    {
      static const char* value() { return DataType<sensor_msgs::PointCloud2>::value(); }
      static const char* value(const pcl::PointCloud<T>&) { return value(); }
    };

    template<typename T> struct Definition<pcl::PointCloud<T> >
    {
      static const char* value() { return Definition<sensor_msgs::PointCloud2>::value(); }
      static const char* value(const pcl::PointCloud<T>&) { return value(); }
    };

    // pcl point clouds message don't have a ROS compatible header
    // the specialized meta functions below (TimeStamp and FrameId)
    // can be used to get the header data.
    template<typename T> struct HasHeader<pcl::PointCloud<T> > : FalseType {};

    template<typename T>
    struct TimeStamp<pcl::PointCloud<T> >
    {
      // This specialization could be dangerous, but it's the best I can do.
      // If this TimeStamp struct is destroyed before they are done with the
      // pointer returned by the first functions may go out of scope, but there
      // isn't a lot I can do about that. This is a good reason to refuse to
      // returning pointers like this...
      static ros::Time* pointer(typename pcl::PointCloud<T> &m) {
        header_.reset(new std_msgs::Header());
        pcl_conversions::fromPCL(m.header, *(header_));
        return &(header_->stamp);
      }
      static ros::Time const* pointer(const typename pcl::PointCloud<T>& m) {
        header_const_.reset(new std_msgs::Header());
        pcl_conversions::fromPCL(m.header, *(header_const_));
        return &(header_const_->stamp);
      }
      static ros::Time value(const typename pcl::PointCloud<T>& m) {
        return pcl_conversions::fromPCL(m.header).stamp;
      }
    private:
      static boost::shared_ptr<std_msgs::Header> header_;
      static boost::shared_ptr<std_msgs::Header> header_const_;
    };

    template<typename T>
    struct FrameId<pcl::PointCloud<T> >
    {
      static std::string* pointer(pcl::PointCloud<T>& m) { return &m.header.frame_id; }
      static std::string const* pointer(const pcl::PointCloud<T>& m) { return &m.header.frame_id; }
      static std::string value(const pcl::PointCloud<T>& m) { return m.header.frame_id; }
    };

  } // namespace ros::message_traits

  namespace serialization 
  {
    template<typename T>
    struct Serializer<pcl::PointCloud<T> >
    {
      template<typename Stream>
      inline static void write(Stream& stream, const pcl::PointCloud<T>& m)
      {
        stream.next(m.header);
        
        // Ease the user's burden on specifying width/height for unorganized datasets
        uint32_t height = m.height, width = m.width;
        if (height == 0 && width == 0) {
          width = m.points.size();
          height = 1;
        }
        stream.next(height);
        stream.next(width);

        // Stream out point field metadata
        typedef typename pcl::traits::fieldList<T>::type FieldList;
        uint32_t fields_size = boost::mpl::size<FieldList>::value;
        stream.next(fields_size);
        pcl::for_each_type<FieldList>(pcl::detail::FieldStreamer<Stream, T>(stream));

        // Assume little-endian...
        uint8_t is_bigendian = false;
        stream.next(is_bigendian);

        // Write out point data as binary blob
        uint32_t point_step = sizeof(T);
        stream.next(point_step);
        uint32_t row_step = point_step * width;
        stream.next(row_step);
        uint32_t data_size = row_step * height;
        stream.next(data_size);
        memcpy(stream.advance(data_size), &m.points[0], data_size);

        uint8_t is_dense = m.is_dense;
        stream.next(is_dense);
      }

      template<typename Stream>
      inline static void read(Stream& stream, pcl::PointCloud<T>& m)
      {
        std_msgs::Header header;
        stream.next(header);
        pcl_conversions::toPCL(header, m.header);
        stream.next(m.height);
        stream.next(m.width);

        /// @todo Check that fields haven't changed!
        std::vector<sensor_msgs::PointField> fields;
        stream.next(fields);

        // Construct field mapping if deserializing for the first time
        static pcl::MsgFieldMap mapping;
        static boost::mutex mutex;
        if (mapping.empty())
        {
          boost::mutex::scoped_lock lock(mutex);
          pcl::createMapping<T>(fields, mapping);
        }

        uint8_t is_bigendian;
        stream.next(is_bigendian); // ignoring...
        uint32_t point_step, row_step;
        stream.next(point_step);
        stream.next(row_step);

        // Copy point data
        uint32_t data_size;
        stream.next(data_size);
        assert(data_size == m.height * m.width * point_step);
        m.points.resize(m.height * m.width);
        uint8_t* m_data = reinterpret_cast<uint8_t*>(&m.points[0]);
        // If the data layouts match, can copy a whole row in one memcpy
        if (mapping.size() == 1 &&
            mapping[0].serialized_offset == 0 &&
            mapping[0].struct_offset == 0 &&
            point_step == sizeof(T))
        {
          uint32_t m_row_step = sizeof(T) * m.width;
          // And if the row steps match, can copy whole point cloud in one memcpy
          if (m_row_step == row_step)
          {
            memcpy (m_data, stream.advance(data_size), data_size);
          }
          else
          {
            for (uint32_t i = 0; i < m.height; ++i, m_data += m_row_step)
              memcpy (m_data, stream.advance(row_step), m_row_step);
          }
        }
        else
        {
          // If not, do a lot of memcpys to copy over the fields
          for (uint32_t row = 0; row < m.height; ++row) {
            const uint8_t* stream_data = stream.advance(row_step);
            for (uint32_t col = 0; col < m.width; ++col, stream_data += point_step) {
              BOOST_FOREACH(const pcl::detail::FieldMapping& fm, mapping) {
                memcpy(m_data + fm.struct_offset, stream_data + fm.serialized_offset, fm.size);
              }
              m_data += sizeof(T);
            }
          }
        }

        uint8_t is_dense;
        stream.next(is_dense);
        m.is_dense = is_dense;
      }

      inline static uint32_t serializedLength(const pcl::PointCloud<T>& m)
      {
        uint32_t length = 0;

        length += serializationLength(m.header);
        length += 8; // height/width

        pcl::detail::FieldsLength<T> fl;
        typedef typename pcl::traits::fieldList<T>::type FieldList;
        pcl::for_each_type<FieldList>(boost::ref(fl));
        length += 4; // size of 'fields'
        length += fl.length;

        length += 1; // is_bigendian
        length += 4; // point_step
        length += 4; // row_step
        length += 4; // size of 'data'
        length += m.points.size() * sizeof(T); // data
        length += 1; // is_dense

        return length;
      }
    };
  } // namespace ros::serialization

  /// @todo Printer specialization in message_operations

} // namespace ros

#endif
