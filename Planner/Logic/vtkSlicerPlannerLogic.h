/*==============================================================================

  Program: 3D Slicer

  Portions (c) Copyright Brigham and Women's Hospital (BWH) All Rights Reserved.

  See COPYRIGHT.txt
  or http://www.slicer.org/copyright/copyright.txt for details.

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

==============================================================================*/

// .NAME vtkSlicerPlannerLogic - slicer logic class for volumes manipulation
// .SECTION Description
// This class manages the logic associated with reading, saving,
// and changing propertied of the volumes


#ifndef __vtkSlicerPlannerLogic_h
#define __vtkSlicerPlannerLogic_h

// Slicer includes
#include "vtkSlicerModuleLogic.h"
#include "vtkMRMLModelNode.h"
#include <vtkSlicerCLIModuleLogic.h>

// MRML includes
#include "vtkMRMLTableNode.h"
#include "vtkPoints.h"
#include "vtkThinPlateSplineTransform.h"
#include "vtkCellLocator.h"
#include "vtkPlane.h"
#include "vtkMatrix4x4.h"

// STD includes
#include <cstdlib>
#include <vector>
#include <map>

//Self includes
#include "vtkSlicerPlannerModuleLogicExport.h"



/// \ingroup Slicer_QtModules_ExtensionTemplate
class VTK_SLICER_PLANNER_MODULE_LOGIC_EXPORT vtkSlicerPlannerLogic :
  public vtkSlicerModuleLogic
{
public:

  static vtkSlicerPlannerLogic* New();
  vtkTypeMacro(vtkSlicerPlannerLogic, vtkSlicerModuleLogic);
  void PrintSelf(ostream& os, vtkIndent indent);

  static const char* DeleteChildrenWarningSettingName();

  // Delete all the children of the given hierarchy node.
  bool DeleteHierarchyChildren(vtkMRMLNode* node);

  void clearModelsAndData();

  void setWrapperLogic(vtkSlicerCLIModuleLogic* logic);
  vtkMRMLCommandLineModuleNode* createPreOPModels(vtkMRMLModelHierarchyNode* HierarchyNode);
  vtkMRMLCommandLineModuleNode* createHealthyBrainModel(vtkMRMLModelNode* brain);
  vtkMRMLCommandLineModuleNode* createBoneTemplateModel(vtkMRMLModelNode* boneTemplate);
  double getPreOPICV();
  double getHealthyBrainICV();
  double getCurrentICV();
  double getTemplateICV();
  vtkMRMLCommandLineModuleNode* createCurrentModel(vtkMRMLModelHierarchyNode* HierarchyNode);
  void finishWrap(vtkMRMLCommandLineModuleNode* cmdNode);
  void fillMetricsTable(vtkMRMLModelHierarchyNode* HierarchyNode, vtkMRMLTableNode* modelMetricsTable);
  vtkMRMLModelNode* getWrappedBrainModel(){return this->HealthyBrain;}
  vtkMRMLModelNode* getWrappedBoneTemplateModel(){return this->BoneTemplate;}

  enum BendModeType
  {
    Single,
    Double,
  };

  enum BendSide
  {
    A,
    B,
  };
  //Bending functions
  void initializeBend(vtkPoints* inputFiducials, vtkMRMLModelNode* model);
  vtkSmartPointer<vtkThinPlateSplineTransform> getBendTransform(double bendMagnitude);
  void clearBendingData();
  vtkSmartPointer<vtkPoints> getSourcePoints() {return this->SourcePoints;}
  vtkSmartPointer<vtkPoints> getTargetPoints() { return this->TargetPoints; }
  void setBendType(BendModeType type) {this->bendMode = type;}
  void setBendSide(BendSide side) { this->bendSide = side; }

protected:
  vtkSlicerPlannerLogic();
  virtual ~vtkSlicerPlannerLogic();
  virtual void SetMRMLSceneInternal(vtkMRMLScene* newScene);
  virtual void UpdateFromMRMLScene();

private:

  vtkSlicerCLIModuleLogic* splitLogic;
  vtkSlicerCLIModuleLogic* wrapperLogic;
  vtkSlicerPlannerLogic(const vtkSlicerPlannerLogic&); // Not implemented
  void operator=(const vtkSlicerPlannerLogic&); // Not implemented
  vtkMRMLCommandLineModuleNode* wrapModel(vtkMRMLModelNode* model, std::string Name, int dest);
  vtkMRMLModelNode* mergeModel(vtkMRMLModelHierarchyNode* HierarchyNode, std::string name);
  void generateSourcePoints();
  vtkVector3d projectToModel(vtkVector3d point);
  vtkVector3d projectToModel(vtkVector3d point, vtkPlane* plane);
  vtkVector3d projectToModel(vtkVector3d point, vtkPolyData* model);
  vtkVector3d projectToModel(vtkVector3d point, vtkCellLocator* locator);
  vtkVector3d getNormalAtPoint(vtkVector3d point, vtkCellLocator* locator, vtkPolyData* model);
  vtkSmartPointer<vtkMatrix4x4> createBendingMatrix(vtkVector3d pointV, double angle);
  vtkSmartPointer<vtkPlane> createPlane(vtkVector3d A, vtkVector3d B, vtkVector3d C, vtkVector3d D);
  void createBendingLocator();
  vtkVector3d bendPoint(vtkVector3d point, double magnitude);
  vtkVector3d bendPoint2(vtkVector3d point, double angle);
  double computeICV(vtkMRMLModelNode* model);
  vtkMRMLModelNode* SkullWrappedPreOP;
  vtkMRMLModelNode* HealthyBrain;
  vtkMRMLModelNode* CurrentModel;
  vtkMRMLModelNode* BoneTemplate;
  vtkMRMLModelNode* TempMerged;
  vtkMRMLModelNode* TempWrapped;

  //Bending member variables
  vtkMRMLModelNode* ModelToBend;
  vtkSmartPointer<vtkPoints> Fiducials;
  vtkSmartPointer<vtkPoints> SourcePoints;
  vtkSmartPointer<vtkPoints> SourcePointsDense;
  vtkSmartPointer<vtkPoints> TargetPoints;
  vtkSmartPointer<vtkCellLocator> cellLocator;
  vtkSmartPointer<vtkCellLocator> BendingPlaneLocator;
  vtkSmartPointer<vtkPlane> BendingPlane;
  vtkSmartPointer<vtkPolyData> BendingPolyData;
  bool bendInitialized;
  BendModeType bendMode;
  BendSide bendSide;


  double preOPICV;
  double healthyBrainICV;
  double currentICV;
  double templateICV;

  enum ModelType
  {
    Current,
    PreOP,
    Template,
    Brain
  };

};

#endif
