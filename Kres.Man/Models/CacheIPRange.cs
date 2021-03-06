﻿using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.Serialization;
using System.Text;

using ProtoBuf;

namespace Kres.Man.Models
{
    [ProtoContract]
    [DataContract]
    public class CacheIPRange
    {
        [ProtoMember(1)]
        [DataMember]
        public IEnumerable<byte> Proto_IpFrom { get; set; }

        [ProtoMember(2)]
        [DataMember]
        public IEnumerable<byte> Proto_IpTo { get; set; }

        [ProtoMember(3)]
        [DataMember]
        public string Identity { get; set; }

        [ProtoMember(4)]
        [DataMember]
        public Int32 PolicyId { get; set; }

        public DateTime Created { get; set; }

        public Int128 IpFrom
        {
            get
            {
                //return Convert.ToUInt64(Proto_Crc64.SelectMany(BitConverter.GetBytes).ToArray());
                var text = Encoding.ASCII.GetString(Proto_IpFrom.ToArray());
                text = text.TrimStart('0');
                var int128 = BigMath.Int128.Parse(text);
                return Int128.Convert(int128);
            }
            set { Proto_IpFrom = Encoding.ASCII.GetBytes(value.ToString()); }
        }
        public Int128 IpTo
        {
            get
            {
                //return Convert.ToUInt64(Proto_Crc64.SelectMany(BitConverter.GetBytes).ToArray());
                var text = Encoding.ASCII.GetString(Proto_IpTo.ToArray());
                text = text.TrimStart('0');
                var int128 = BigMath.Int128.Parse(text);
                return Int128.Convert(int128);
            }
            set { Proto_IpFrom = Encoding.ASCII.GetBytes(value.ToString()); }
        }

        public BigMath.Int128 BintFrom
        {
            get
            {
                return new BigMath.Int128(IpFrom.Hi, IpFrom.Low);
            }
        }
        public BigMath.Int128 BintTo
        {
            get
            {
                return new BigMath.Int128(IpTo.Hi, IpTo.Low);
            }
        }

        public string Text
        {
            get { return Encoding.ASCII.GetString(Proto_IpTo.ToArray()).TrimStart('0'); }
        }
        public string ParsedText
        {
            get { return BigMath.Int128.Parse(Encoding.ASCII.GetString(Proto_IpTo.ToArray()).TrimStart('0')).ToString(); }
        }
    }
}
