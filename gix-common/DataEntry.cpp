#include "DataEntry.h"

#define PIC_ALPHABETIC 		0x01
#define PIC_NUMERIC 		0x02
#define PIC_NATIONAL		0x04
#define PIC_ALPHANUMERIC	(PIC_ALPHABETIC | PIC_NUMERIC)

DataEntry::DataEntry()
{
	occurs = 0;
	level = 0;
	storage_size = 0;
	storage = "";
	storage_type = WsEntryStorageType::Unknown;
	decimals = 0;
	line = 0;
	fileid = 0;
	lst_line = 0;
	parent = nullptr;
	not_ref = false;
	ref_by_child = false;
	ref_by_parent = false;
	included = false;
	type = WsEntryType::Unknown;
	offset_data_section = 0;
	offset_local = 0;
	display_size = 0;
#ifndef GIX_HTTP
	owner = nullptr;
#endif
#ifdef GIX_HTTP
	is_required = false;
#endif
}

DataEntry::DataEntry(const QString &name, const QString &path) : DataEntry()
{
	this->name = name;
	this->path = path;
}

DataEntry::~DataEntry()
{
	for (WsReference* ref : references) {
		if (ref)
			delete ref;
	}

	//for (DataEntry * e: children) {
	//	if (e)
	//		delete e;
	//}
}

DataEntry *DataEntry::getTopMostParent()
{
	DataEntry *p = this;
	while (p) {
		if (!p->parent || p->parent->is_placeholder)
			break;

		p = p->parent;
	}
	return p;
}

bool DataEntry::isFiller()
{
	return this->name.toUpper() == "FILLER" || this->type == WsEntryType::Filler;
}

bool DataEntry::isGroup()
{
	return this->children.size() > 0;
}

DataEntry *DataEntry::fromCobolRawField(cb_field_ptr p)
{
	DataEntry *e = new DataEntry();
	e->name = p->sname;
	e->line = p->defined_at_source_line;
	e->filename = p->defined_at_source_file;
	e->level = p->level;
	e->decimals = p->scale;
	e->is_signed = p->have_sign;
	e->occurs = p->occurs;
	
	QString format;

	if (!p->children) {
		switch (p->pictype) {
			case PIC_ALPHABETIC:
				e->type = WsEntryType::Alphabetic;
				e->storage_type = WsEntryStorageType::Literal;
				e->storage_size = p->picnsize;
				e->display_size = p->picnsize;
				format = "PIC A(" + QString::number(p->picnsize) + ")";
				break;

			case PIC_ALPHANUMERIC:
				e->type = WsEntryType::Alphanumeric;
				e->storage_type = WsEntryStorageType::Literal;
				e->storage_size = p->picnsize;
				e->display_size = p->picnsize;
				format = "PIC X(" + QString::number(p->picnsize) + ")";
				break;

			case PIC_NUMERIC:
				e->type = WsEntryType::Alphabetic;
				e->display_size = p->picnsize + (p->have_sign ? 1 : 0) + (p->scale > 0 ? 1 : 0);	// TODO: this is most likely wrong
				QString sign = p->have_sign ? "S" : "";
				format = "PIC " + sign + "9(" + QString::number(p->picnsize) + ")";

				if (p->scale)
					format += "V(" + QString::number(p->scale) + ")";

				int bsize = p->picnsize + p->scale;	// TODO: verify this

				if (p->usage != Usage::None) {
					switch (p->usage) {
						case Usage::Binary:
							format += " USAGE BINARY";
							e->storage_type = WsEntryStorageType::Comp5;
							break;
						case Usage::Float:
							format += " USAGE COMP-1";
							e->storage_type = WsEntryStorageType::Comp5;
							break;
						case Usage::Double:
							format += " USAGE COMP-2";
							e->storage_type = WsEntryStorageType::Comp5;
							break;
						case Usage::Packed:
							format += " USAGE COMP-3";
							e->storage_type = WsEntryStorageType::Comp3;
							e->storage_size = (bsize / 2) + 1;
							break;
					}
				}
				else {
					e->storage_type = WsEntryStorageType::Literal;
					e->storage_size = p->picnsize + (p->have_sign ? 1 : 0);
				}
				break;
		}
		e->format = format;
	}
	return e;
}

int DataEntry::computeTotalStorageSize()
{
	int sum_ch = this->storage_size;
	for (DataEntry *c : this->children) {
		sum_ch += c->computeTotalStorageSize();
	}

	return sum_ch;
}

WsReference::WsReference()
{
	line = 0;
	is_write_reference = false;
}