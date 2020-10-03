#pragma once
#include "GenericEditor.h"
#include <Tera/UStaticMesh.h>
#include <Tera/UTexture.h>

#include "../Misc/OSGWindow.h"

class StaticMeshEditor : public GenericEditor {
public:
  using GenericEditor::GenericEditor;
  StaticMeshEditor(wxPanel* parent, PackageWindow* window);
  ~StaticMeshEditor() override;

  void OnTick() override;
  void OnObjectLoaded() override;

  void OnExportClicked(wxCommandEvent&) override;

protected:
  void CreateRenderer();
  void CreateRenderModel();

protected:
  UStaticMesh* Mesh = nullptr;
  osg::ref_ptr<osg::Geode> Root = nullptr;
  OSGCanvas* Canvas = nullptr;
  OSGWindow* OSGProxy = nullptr;
  osgViewer::Viewer* Renderer = nullptr;
};