#include <exiv2/exif.hpp>
#include <exiv2/xmp_exiv2.hpp>
#include <exiv2/iptc.hpp>

extern "C" {
#include "metadata.h"
}

// Disclaimer: I don't know any C++

template <typename Datum>
static void print_any(Datum meta, struct wu_tree *tree) {
	std::string group = meta->groupName();

	struct wu_tree *branch = tree_findadd_branch(tree, group.c_str());
	if (branch) {
		std::string tag = meta->tagName();

		if (meta->count() == 1) {
			struct wu_leaf leaf;
			switch (meta->typeId()) {
			case Exiv2::TypeId::unsignedByte:
			case Exiv2::TypeId::unsignedShort:
			case Exiv2::TypeId::signedByte:
			case Exiv2::TypeId::signedShort:
			case Exiv2::TypeId::signedLong:
				leaf.val.d = meta->toLong();
				leaf.type = wu_leaf_signed;
				tree_bud_leaf(branch, tag.c_str(), leaf);
				return;
			case Exiv2::TypeId::tiffFloat:
			case Exiv2::TypeId::tiffDouble:
				leaf.val.g = meta->toFloat();
				leaf.type = wu_leaf_double;
				tree_bud_leaf(branch, tag.c_str(), leaf);
				return;
			default:
				break;
			}
		}
		std::string val = meta->toString();
		tree_sprout_measured_leaf(branch, tag.c_str(), val.data(),
			val.size());
	}
}

static void print_xmp(const char *metadata, const size_t len,
struct wu_tree *tree) {
	const std::string str_xmp(metadata, len);

	Exiv2::XmpData data;
	Exiv2::XmpParser::decode(data, str_xmp);
	if (!data.count()) {
		return;
	}

	Exiv2::XmpData::const_iterator end = data.end();
	struct wu_tree *outtree = tree_sprout_branch(tree, "XMP");
	for (Exiv2::XmpData::const_iterator i = data.begin(); i != end; ++i) {
		if (i->count()) {
			print_any(i, outtree);
		}
	}
	data.clear();
}

static void print_iptc(const unsigned char *metadata, const size_t len,
struct wu_tree *tree) {
	Exiv2::IptcData data;
	Exiv2::IptcParser::decode(data, metadata, len);
	if (!data.count()) {
		return;
	}

	Exiv2::IptcData::const_iterator end = data.end();
	struct wu_tree *outtree = tree_sprout_branch(tree, "IPTC");
	for (Exiv2::IptcData::const_iterator i = data.begin(); i != end; ++i) {
		if (i->count()) {
			print_any(i, outtree);
		}
	}
	data.clear();
}

static void print_exif(const unsigned char *metadata, const size_t len,
struct wu_tree *tree) {
	Exiv2::ExifData data;
	Exiv2::ExifParser::decode(data, metadata, len);
	if (!data.count()) {
		return;
	}

	Exiv2::ExifData::const_iterator end = data.end();
	struct wu_tree *outtree = tree_sprout_branch(tree, "Exif");
	for (Exiv2::ExifData::const_iterator i = data.begin(); i != end; ++i) {
		if (i->count()) {
			print_any(i, outtree);
		}
	}
	data.clear();
}

extern "C" void standard_metadata(const enum metadata_type type,
const void *metadata, const size_t len, struct wu_tree *tree) {
	try {
		switch (type) {
		case exif_metadata:
			print_exif((const unsigned char *)metadata, len, tree);
			break;
		case xmp_metadata:
			print_xmp((const char *)metadata, len, tree);
			break;
		case iptc_metadata:
			print_iptc((const unsigned char *)metadata, len, tree);
			break;
		default:
			break;
		}
	} catch (...) {
		switch (type) {
		case exif_metadata: fputs("Exif", stdout); break;
		case xmp_metadata: fputs("XMP", stdout); break;
		case iptc_metadata: fputs("IPTC", stdout); break;
		}
		fputs(" parsing failed.\n", stdout);
	}
}
