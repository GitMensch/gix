#include "ProjectFilePropertyDefinitionCollection.h"
//#include "SoapOptionsDialog.h"
//#include "RestOptionsDialog.h"
#include "SysUtils.h"
#include "ProjectFile.h"
#include "PropertyConsts.h"

#include <QMap>
#include <QVariantMap>
#include <QString>

ProjectFilePropertyDefinitionCollection::ProjectFilePropertyDefinitionCollection()
{
	build_action_opts = new QMap<QString, QVariant>();
	build_action_opts->insert("compile", "Compile");
	build_action_opts->insert("copy", "Copy");
	build_action_opts->insert("none", "None");

	build_type_opts = new QMap<QString, QVariant>();
	build_type_opts->insert("", "None (project default)");
	build_type_opts->insert("exe", "Executable program");
	build_type_opts->insert("dll", "Dynamically loadable program");

	//QMap<QString, QVariant> m_soap;
	//m_soap.insert("enabled", false);
	//m_soap.insert("dialog_type", "SoapOptionsDialog");
	//m_soap.insert("port", "8090");
	//QString v_soap = SysUtils::serializeMap(&m_soap);

	QMap<QString, QVariant> m_rest;
	m_rest.insert("enabled", false);
	m_rest.insert("dialog_type", "RestOptionsDialog");
	m_rest.insert("port", "9090");
	QString v_rest = SysUtils::serializeMap(&m_rest);

	defs.append(new PropertyDefinition("build_action", tr("Build action"), PropertyType::PropertyTypeOption, "compile", false, build_action_opts));
	defs.append(new PropertyDefinition("filepath", tr("File path"), PropertyType::PropertyTypeText, "", true));

	PropertyDefinition *is_startup_item = new PropertyDefinition(PropertyConsts::IsStartupItem, tr("Is startup item/module"), PropertyType::PropertyTypeBoolean, false, false);
	//is_startup_item->show_depending_on = [](PropertySource *ps) { 
	//	ProjectFile *pf = dynamic_cast<ProjectFile *>(ps);
	//	if (pf) {
	//		Project *prj = pf->getParentProject();
	//		if (prj)
	//			return prj->getType() == ProjectType::SingleBinary || prj->getType() == ProjectType::MultipleBinaries;
	//	}
	//	return true;
	//};
	defs.append(is_startup_item);

	PropertyDefinition *custom_build_type = new PropertyDefinition(PropertyConsts::CustomBuildType, tr("Custom build type"), PropertyType::PropertyTypeOption, "", false, build_type_opts);
	custom_build_type->show_depending_on = [](PropertySource *ps) {
		ProjectFile *pf = dynamic_cast<ProjectFile *>(ps);
		if (pf) {
			Project *prj = pf->getParentProject();
			if (prj)
				return prj->getType() == ProjectType::MultipleBinaries;
		}
		return true;
	};
	defs.append(custom_build_type);

	//defs.append(new PropertyDefinition("is_soap_ws", tr("Expose as SOAP Web Service"), PropertyType::PropertyTypeConditionalPropertySet, v_soap, false));
	//defs.append(new PropertyDefinition("is_rest_ws", tr("Expose as REST Web Service"), PropertyType::PropertyTypeConditionalPropertySet, v_rest, false));

	PropertyDefinition *is_rest_ws = new PropertyDefinition("is_rest_ws", tr("Expose as REST Web Service"), PropertyType::PropertyTypeConditionalPropertySet, v_rest, false);
	is_rest_ws->show_depending_on = [](PropertySource *ps) {
		ProjectFile *pf = dynamic_cast<ProjectFile *>(ps);
		if (pf) {
			Project *prj = pf->getParentProject();
			if (prj)
				return prj->getType() == ProjectType::Web;
		}
		return true;
	};
	defs.append(is_rest_ws);
}


ProjectFilePropertyDefinitionCollection::~ProjectFilePropertyDefinitionCollection()
{
}
