#include <exiv2/exif.hpp>
#include <exiv2/xmp_exiv2.hpp>
#include <exiv2/iptc.hpp>

extern "C" {
#include "metadata.h"
#include "common.h"
}

static const char EXIF[] = "Exif";
static const char XMP[] = "XMP";
static const char IPTC[] = "IPTC";

// Disclaimer: I don't know any C++

template <typename Datum>
static void read_any(Datum meta, struct wu_tree *tree) {
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
				leaf.val.f = meta->toFloat();
				leaf.type = wu_leaf_float;
				tree_bud_leaf(branch, tag.c_str(), leaf);
				return;
			default:
				break;
			}
		}
		std::string val = meta->toString();
		tree_add_leaf_len(branch, tag.c_str(), val.data(),
			val.size(), NULL);
	}
}

static bool read_xmp(const char *metadata, const size_t len,
struct wu_tree *tree) {
	const std::string str_xmp(metadata, len);

	Exiv2::XmpData data;
	Exiv2::XmpParser::decode(data, str_xmp);
	if (!data.count()) {
		return false;
	}

	Exiv2::XmpData::const_iterator end = data.end();
	struct wu_tree *outtree = tree_add_branch(tree, XMP);
	for (Exiv2::XmpData::const_iterator i = data.begin(); i != end; ++i) {
		if (i->count()) {
			read_any(i, outtree);
		}
	}
	data.clear();
	return true;
}

static bool read_iptc(const unsigned char *metadata, const size_t len,
struct wu_tree *tree) {
	Exiv2::IptcData data;
	Exiv2::IptcParser::decode(data, metadata, len);
	if (!data.count()) {
		return false;
	}

	Exiv2::IptcData::const_iterator end = data.end();
	struct wu_tree *outtree = tree_add_branch(tree, IPTC);
	for (Exiv2::IptcData::const_iterator i = data.begin(); i != end; ++i) {
		if (i->count()) {
			read_any(i, outtree);
		}
	}
	data.clear();
	return true;
}

static bool read_exif(const unsigned char *metadata, const size_t len,
struct wu_tree *tree) {
	Exiv2::ExifData data;
	Exiv2::ExifParser::decode(data, metadata, len);
	if (!data.count()) {
		return false;
	}

	Exiv2::ExifData::const_iterator end = data.end();
	struct wu_tree *outtree = tree_add_branch(tree, EXIF);
	for (Exiv2::ExifData::const_iterator i = data.begin(); i != end; ++i) {
		if (i->count()) {
			read_any(i, outtree);
		}
	}
	data.clear();
	return true;
}

extern "C" unsigned char metadata_orientation(struct wu_tree *tree) {
	const char *path[] = {EXIF, "Image", "Orientation"};
	tree = tree_find_path(tree, path, ARRAY_LEN(path));
	if (tree && tree->leaf.type == wu_leaf_signed) {
		const long val = tree->leaf.val.d;
		if (val > 0 && val <= 8) {
			return (unsigned char)val;
		}
	}
	return 0;
}

extern "C" bool standard_metadata(const enum metadata_type type,
const void *metadata, const size_t len, struct wu_tree *tree) {
	bool st = false;
	try {
		switch (type) {
		case exif_metadata:
			st = read_exif((const unsigned char *)metadata, len, tree);
			break;
		case xmp_metadata:
			st = read_xmp((const char *)metadata, len, tree);
			break;
		case iptc_metadata:
			st = read_iptc((const unsigned char *)metadata, len, tree);
			break;
		}
	} catch (...) {
		switch (type) {
		case exif_metadata: fputs(EXIF, stderr); break;
		case xmp_metadata: fputs(XMP, stderr); break;
		case iptc_metadata: fputs(IPTC, stderr); break;
		}
		fputs(" parsing failed.\n", stderr);
	}
	return st;
}
