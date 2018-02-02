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

// Planner Logic includes
#include "vtkSlicerPlannerLogic.h"

// Slicer CLI includes
#include <qSlicerCoreApplication.h>
#include <qSlicerModuleManager.h>
#include "qSlicerAbstractCoreModule.h"
#include <qSlicerCLIModule.h>
#include <vtkSlicerCLIModuleLogic.h>

// MRML includes
#include <vtkMRMLScene.h>
#include <vtkMRMLHierarchyNode.h>
#include <vtkMRMLModelHierarchyNode.h>
#include <vtkMRMLModelStorageNode.h>
#include <vtkMRMLModelDisplayNode.h>

// VTK includes
#include <vtkNew.h>
#include <vtkMassProperties.h>
#include <vtkTriangleFilter.h>
#include <vtkAppendPolyData.h>
#include "vtkVector.h"
#include "vtkVectorOperators.h"
#include "vtkMath.h"
#include "vtkCutter.h"
#include "vtkPointData.h"
#include "vtkCellData.h"
#include "vtkPolyDataNormals.h"
#include "vtkCleanPolyData.h"
#include "vtkPolyDataPointSampler.h"
#include "vtkDecimatePro.h"
#include "vtkMatrix4x4.h"
#include "vtkVertexGlyphFilter.h"

// STD includes
#include <cassert>
#include <sstream>


//----------------------------------------------------------------------------
vtkStandardNewMacro(vtkSlicerPlannerLogic);

//----------------------------------------------------------------------------
//Constructor
vtkSlicerPlannerLogic::vtkSlicerPlannerLogic()
{
  this->SkullWrappedPreOP = NULL;
  this->HealthyBrain = NULL;
  this->BoneTemplate = NULL;
  this->splitLogic = NULL;
  this->wrapperLogic = NULL;
  this->preOPICV = 0;
  this->healthyBrainICV = 0;
  this->currentICV = 0;
  this->templateICV = 0;
  this->TempMerged = NULL;
  this->TempWrapped = NULL;
  this->CurrentModel = NULL;
  this->SourcePoints = NULL;
  this->SourcePointsDense = NULL;
  this->TargetPoints = NULL;
  this->Fiducials = NULL;
  this->cellLocator = NULL;
  this->bendMode = Double;
  this->bendSide = A;
  this->BendingPlane = NULL;
  this->BendingPlaneLocator = NULL;
  this->bendInitialized = false;
  this->BendingPolyData = NULL;
}

//----------------------------------------------------------------------------
//Destructor
vtkSlicerPlannerLogic::~vtkSlicerPlannerLogic()
{
}

//-----------------------------------------------------------------------------
const char* vtkSlicerPlannerLogic::DeleteChildrenWarningSettingName()
{
  return "Planner/DeleteChildrenWarning";
}

//----------------------------------------------------------------------------
bool vtkSlicerPlannerLogic::DeleteHierarchyChildren(vtkMRMLNode* node)
{
  vtkMRMLHierarchyNode* hNode = vtkMRMLHierarchyNode::SafeDownCast(node);
  if(!hNode)
  {
    vtkErrorMacro("DeleteHierarchyChildren: Not a hierarchy node.");
    return false;
  }
  if(!this->GetMRMLScene())
  {
    vtkErrorMacro("DeleteHierarchyChildren: No scene defined on this class");
    return false;
  }

  // first off, set up batch processing mode on the scene
  this->GetMRMLScene()->StartState(vtkMRMLScene::BatchProcessState);

  // get all the children nodes
  std::vector< vtkMRMLHierarchyNode*> allChildren;
  hNode->GetAllChildrenNodes(allChildren);

  // and loop over them
  for(unsigned int i = 0; i < allChildren.size(); ++i)
  {
    vtkMRMLNode* associatedNode = allChildren[i]->GetAssociatedNode();
    if(associatedNode)
    {
      this->GetMRMLScene()->RemoveNode(associatedNode);
    }
    this->GetMRMLScene()->RemoveNode(allChildren[i]);
  }
  // end batch processing
  this->GetMRMLScene()->EndState(vtkMRMLScene::BatchProcessState);

  return true;
}

//----------------------------------------------------------------------------
void vtkSlicerPlannerLogic::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//---------------------------------------------------------------------------
void vtkSlicerPlannerLogic::SetMRMLSceneInternal(vtkMRMLScene* newScene)
{
  Superclass::SetMRMLSceneInternal(newScene);
}

//---------------------------------------------------------------------------
void vtkSlicerPlannerLogic::UpdateFromMRMLScene()
{
  assert(this->GetMRMLScene() != 0);
}

//----------------------------------------------------------------------------
//Set logic for Shrink Wrap CLI
void vtkSlicerPlannerLogic::setWrapperLogic(vtkSlicerCLIModuleLogic* logic)
{
  this->wrapperLogic = logic;
}

//----------------------------------------------------------------------------
//Create reference model form current hierarhcy state
vtkMRMLCommandLineModuleNode* vtkSlicerPlannerLogic::createPreOPModels(vtkMRMLModelHierarchyNode* HierarchyNode)
{
  if(this->SkullWrappedPreOP)
  {
    this->GetMRMLScene()->RemoveNode(this->SkullWrappedPreOP);
    this->SkullWrappedPreOP = NULL;
  }

  std::string name;
  name = HierarchyNode->GetName();
  name += " - Merged";
  this->TempMerged = this->mergeModel(HierarchyNode, name);
  this->TempMerged->GetDisplayNode()->SetVisibility(0);
  name = HierarchyNode->GetName();
  name += " - Wrapped";
  return this->wrapModel(this->TempMerged, name, vtkSlicerPlannerLogic::PreOP);
}

//----------------------------------------------------------------------------
//Get the pre-op ICV
double vtkSlicerPlannerLogic::getPreOPICV()
{
  if(this->SkullWrappedPreOP)
  {
    this->preOPICV = this->computeICV(this->SkullWrappedPreOP);
  }

  return this->preOPICV;
}

//----------------------------------------------------------------------------
//Create wrapped model from current hierarchy
vtkMRMLCommandLineModuleNode*  vtkSlicerPlannerLogic::createCurrentModel(vtkMRMLModelHierarchyNode* HierarchyNode)
{
  if(this->CurrentModel)
  {
    this->GetMRMLScene()->RemoveNode(this->CurrentModel);
    this->CurrentModel = NULL;
  }
  std::string name;
  name = HierarchyNode->GetName();
  name += " - Temp Merge";
  this->TempMerged = this->mergeModel(HierarchyNode, name);
  this->TempMerged->GetDisplayNode()->SetVisibility(0);
  name = HierarchyNode->GetName();
  name += " - Current Wrapped";
  return this->wrapModel(this->TempMerged, name, vtkSlicerPlannerLogic::Current);
}

//----------------------------------------------------------------------------
//Get the current ICV
double vtkSlicerPlannerLogic::getCurrentICV()
{
  if(this->CurrentModel)
  {
    this->currentICV = this->computeICV(this->CurrentModel);
  }
  return this->currentICV;
}

//----------------------------------------------------------------------------
//Create wrapped version of brain model input
vtkMRMLCommandLineModuleNode* vtkSlicerPlannerLogic::createHealthyBrainModel(vtkMRMLModelNode* model)
{
  if(this->HealthyBrain)
  {
    this->GetMRMLScene()->RemoveNode(this->HealthyBrain);
    this->HealthyBrain = NULL;
  }

  std::string name;
  name = model->GetName();
  name += " - Wrapped";
  return wrapModel(model, name, vtkSlicerPlannerLogic::Brain);
}

//----------------------------------------------------------------------------
//Get brain ICV
double vtkSlicerPlannerLogic::getHealthyBrainICV()
{
  if(this->HealthyBrain)
  {
    this->healthyBrainICV = this->computeICV(this->HealthyBrain);
  }
  return this->healthyBrainICV;
}

//----------------------------------------------------------------------------
//Create wrapped version of bone template input
vtkMRMLCommandLineModuleNode* vtkSlicerPlannerLogic::createBoneTemplateModel(vtkMRMLModelNode* model)
{
  if (this->BoneTemplate)
  {
    this->GetMRMLScene()->RemoveNode(this->BoneTemplate);
    this->BoneTemplate = NULL;
  }

  std::string name;
  name = model->GetName();
  name += " - Wrapped";
  return wrapModel(model, name, vtkSlicerPlannerLogic::Template);
}

//----------------------------------------------------------------------------
//Get template ICV
double vtkSlicerPlannerLogic::getTemplateICV()
{
  if (this->BoneTemplate)
  {
    this->templateICV = this->computeICV(this->BoneTemplate);
  }
  return this->templateICV;
}

//----------------------------------------------------------------------------
//Merge hierarchy into a single model
vtkMRMLModelNode* vtkSlicerPlannerLogic::mergeModel(vtkMRMLModelHierarchyNode* HierarchyNode, std::string name)
{

  vtkNew<vtkMRMLModelNode> mergedModel;
  vtkNew<vtkAppendPolyData> filter;
  vtkMRMLScene* scene = this->GetMRMLScene();
  mergedModel->SetScene(scene);
  mergedModel->SetName(name.c_str());
  vtkNew<vtkMRMLModelDisplayNode> dnode;
  vtkNew<vtkMRMLModelStorageNode> snode;
  mergedModel->SetAndObserveDisplayNodeID(dnode->GetID());
  mergedModel->SetAndObserveStorageNodeID(snode->GetID());
  scene->AddNode(dnode.GetPointer());
  scene->AddNode(snode.GetPointer());
  scene->AddNode(mergedModel.GetPointer());

  std::vector<vtkMRMLHierarchyNode*> children;
  std::vector<vtkMRMLHierarchyNode*>::const_iterator it;
  HierarchyNode->GetAllChildrenNodes(children);
  for(it = children.begin(); it != children.end(); ++it)
  {
    vtkMRMLModelNode* childModel =
      vtkMRMLModelNode::SafeDownCast((*it)->GetAssociatedNode());

    if(childModel)
    {
      filter->AddInputData(childModel->GetPolyData());

    }
  }

  filter->Update();
  mergedModel->SetAndObservePolyData(filter->GetOutput());
  mergedModel->SetAndObserveDisplayNodeID(dnode->GetID());

  return mergedModel.GetPointer();
}

//----------------------------------------------------------------------------
//Compute the ICV of a model
double vtkSlicerPlannerLogic::computeICV(vtkMRMLModelNode* model)
{
  vtkNew<vtkMassProperties> areaFilter;
  vtkNew<vtkTriangleFilter> triFilter;
  triFilter->SetInputData(model->GetPolyData());
  triFilter->Update();
  areaFilter->SetInputData(triFilter->GetOutput());
  areaFilter->Update();
  return (areaFilter->GetVolume() / 1000);   //convert to cm^3
}

//----------------------------------------------------------------------------
//Create shrink wrapped version of a model
vtkMRMLCommandLineModuleNode* vtkSlicerPlannerLogic::wrapModel(vtkMRMLModelNode* model, std::string name, int dest)
{
  vtkNew<vtkMRMLModelNode> wrappedModel;
  vtkMRMLScene* scene = this->GetMRMLScene();
  wrappedModel->SetScene(scene);
  wrappedModel->SetName(name.c_str());
  vtkNew<vtkMRMLModelDisplayNode> dnode;
  vtkNew<vtkMRMLModelStorageNode> snode;
  wrappedModel->SetAndObserveDisplayNodeID(dnode->GetID());
  wrappedModel->SetAndObserveStorageNodeID(snode->GetID());
  scene->AddNode(dnode.GetPointer());
  scene->AddNode(snode.GetPointer());
  scene->AddNode(wrappedModel.GetPointer());

  switch(dest)
  {
  case vtkSlicerPlannerLogic::Current:
    this->CurrentModel = wrappedModel.GetPointer();
    break;
  case vtkSlicerPlannerLogic::PreOP:
    this->SkullWrappedPreOP = wrappedModel.GetPointer();
    break;
  case vtkSlicerPlannerLogic::Brain:
    this->HealthyBrain = wrappedModel.GetPointer();
    break;
  case vtkSlicerPlannerLogic::Template:
    this->BoneTemplate = wrappedModel.GetPointer();

  }

  //CLI setup
  this->wrapperLogic->SetMRMLScene(this->GetMRMLScene());
  vtkMRMLCommandLineModuleNode* cmdNode = this->wrapperLogic->CreateNodeInScene();
  cmdNode->SetParameterAsString("inputModel", model->GetID());
  cmdNode->SetParameterAsString("outputModel", wrappedModel->GetID());
  cmdNode->SetParameterAsString("PhiRes", "20");
  cmdNode->SetParameterAsString("ThetaRes", "20");
  this->wrapperLogic->Apply(cmdNode, true);
  return cmdNode;
}

//----------------------------------------------------------------------------
//Finish up wrapper CLI
void vtkSlicerPlannerLogic::finishWrap(vtkMRMLCommandLineModuleNode* cmdNode)
{
  vtkMRMLModelNode* node = vtkMRMLModelNode::SafeDownCast(this->GetMRMLScene()->GetNodeByID(cmdNode->GetParameterAsString("outputModel")));
  node->GetDisplayNode()->SetVisibility(0);
  this->GetMRMLScene()->RemoveNode(cmdNode);

  if(this->TempMerged)
  {
    this->GetMRMLScene()->RemoveNode(this->TempMerged);
    this->TempMerged = NULL;
  }

  if(this->TempWrapped)
  {
    this->GetMRMLScene()->RemoveNode(this->TempWrapped);
    this->TempWrapped = NULL;
  }
}

//----------------------------------------------------------------------------
//Fill table node with metrics
void vtkSlicerPlannerLogic::fillMetricsTable(vtkMRMLModelHierarchyNode* HierarchyNode, vtkMRMLTableNode* modelMetricsTable)
{
  double preOpVolume;
  double currentVolume;
  double brainVolume;
  double templateVolume;
  if(HierarchyNode)
  {
    preOpVolume = this->getPreOPICV();
    brainVolume = this->getHealthyBrainICV();
    currentVolume = this->getCurrentICV();
    templateVolume = this->getTemplateICV();

    modelMetricsTable->RemoveAllColumns();
    std::string modelTableName = "Model Metrics - ";
    modelTableName += HierarchyNode->GetName();
    modelMetricsTable->SetName(modelTableName.c_str());

    vtkAbstractArray* col0 = modelMetricsTable->AddColumn();
    vtkAbstractArray* col1 = modelMetricsTable->AddColumn();
    col1->SetName("Healthy Brain");
    vtkAbstractArray* col2 = modelMetricsTable->AddColumn();
    col2->SetName("Bone Template");
    vtkAbstractArray* col3 = modelMetricsTable->AddColumn();
    col3->SetName("Pre-op");
    vtkAbstractArray* col4 = modelMetricsTable->AddColumn();
    col4->SetName("Current");
    modelMetricsTable->SetUseColumnNameAsColumnHeader(true);
    modelMetricsTable->SetUseFirstColumnAsRowHeader(true);
    modelMetricsTable->SetLocked(true);

    int r1 = modelMetricsTable->AddEmptyRow();
    modelMetricsTable->SetCellText(0, 0, "ICV\n cm^3");
    
    std::stringstream brainVolumeSstr;
    brainVolumeSstr << brainVolume;
    const std::string& brainVolumeString = brainVolumeSstr.str();
    modelMetricsTable->SetCellText(0, 1, brainVolumeString.c_str());

    std::stringstream templateVolumeSstr;
    templateVolumeSstr << templateVolume;
    const std::string& templateVolumeString = templateVolumeSstr.str();
    modelMetricsTable->SetCellText(0, 2, templateVolumeString.c_str());
    
    std::stringstream preOpVolumeSstr;
    preOpVolumeSstr << preOpVolume;
    const std::string& preOpVolumeString = preOpVolumeSstr.str();
    modelMetricsTable->SetCellText(0, 3, preOpVolumeString.c_str());
    
    std::stringstream currentVolumeSstr;
    currentVolumeSstr << currentVolume;
    const std::string& currentVolumeString = currentVolumeSstr.str();
    modelMetricsTable->SetCellText(0, 4, currentVolumeString.c_str());
    
  }
}

//----------------------------------------------------------------------------
//Initiaize bending
void vtkSlicerPlannerLogic::initializeBend(vtkPoints* inputFiducials, vtkMRMLModelNode* model)
{
  this->Fiducials = inputFiducials;
  vtkNew<vtkTriangleFilter> triangulate;
  vtkNew<vtkCleanPolyData> clean;
  this->ModelToBend = model;
  clean->SetInputData(this->ModelToBend->GetPolyData());
  clean->Update();
  this->BendingPolyData = clean->GetOutput();

  this->cellLocator = vtkSmartPointer<vtkCellLocator>::New();
  this->cellLocator->SetDataSet(this->BendingPolyData);
  this->cellLocator->BuildLocator();

  this->generateSourcePoints();
  this->bendInitialized =  true;
}

//----------------------------------------------------------------------------
//CReate bend transform based on points and bend magnitude
vtkSmartPointer<vtkThinPlateSplineTransform> vtkSlicerPlannerLogic::getBendTransform(double magnitude)
{
  vtkSmartPointer<vtkThinPlateSplineTransform> transform = vtkSmartPointer<vtkThinPlateSplineTransform>::New();
  if(this->bendInitialized)
  {

    this->TargetPoints = vtkSmartPointer<vtkPoints>::New();
    for(int i = 0; i < this->SourcePointsDense->GetNumberOfPoints(); i++)
    {
      double p[3];
      this->SourcePointsDense->GetPoint(i, p);
      vtkVector3d point = (vtkVector3d)p;
      vtkVector3d bent = point;
      if(this->bendMode == Double)
      {
        bent = this->bendPoint2(point, magnitude);
      }
      if(this->bendMode == Single)
      {
        if(this->bendSide == A)
        {
          if(this->BendingPlane->EvaluateFunction(point.GetData())*this->BendingPlane->EvaluateFunction(this->SourcePoints->GetPoint(0)) > 0)
          {
            bent = this->bendPoint2(point, magnitude);
          }
        }
        if(this->bendSide == B)
        {
          if(this->BendingPlane->EvaluateFunction(point.GetData())*this->BendingPlane->EvaluateFunction(this->SourcePoints->GetPoint(1)) > 0)
          {
            bent = this->bendPoint2(point, magnitude);
          }
        }

      }
      this->TargetPoints->InsertPoint(i, bent.GetData());
    }

    transform->SetSigma(.0001);
    transform->SetBasisToR();
    transform->SetSourceLandmarks(this->SourcePointsDense);
    transform->SetTargetLandmarks(this->TargetPoints);
    transform->Update();
  }
  return transform;
}

//----------------------------------------------------------------------------
//Clear all bending data
void vtkSlicerPlannerLogic::clearBendingData()
{
  this->SourcePoints = NULL;
  this->SourcePointsDense = NULL;
  this->TargetPoints = NULL;
  this->Fiducials = NULL;
  this->ModelToBend = NULL;
  this->cellLocator = NULL;
  this->BendingPlane = NULL;
  this->BendingPlaneLocator = NULL;
  this->bendInitialized = false;
}

//----------------------------------------------------------------------------
//Create source points based on fiducials
void vtkSlicerPlannerLogic::generateSourcePoints()
{
  this->SourcePoints = vtkSmartPointer<vtkPoints>::New();
  double bounds[6];
  this->ModelToBend->GetBounds(bounds);
  double xspan = bounds[1] - bounds[0];
  double yspan = bounds[3] - bounds[2];
  double zspan = bounds[5] - bounds[4];

  double maxspan = vtkMath::Max(xspan, yspan);
  maxspan = vtkMath::Max(maxspan, zspan);

  double c[3];
  double d[3];
  this->Fiducials->GetPoint(2, c);
  this->Fiducials->GetPoint(3, d);

  double a[3];
  double b[3];
  this->Fiducials->GetPoint(0, a);
  this->Fiducials->GetPoint(1, b);

  vtkVector3d C = (vtkVector3d)c;
  vtkVector3d D = (vtkVector3d)d;

  vtkVector3d CD = D - C;
  CD.Normalize();

  C = C - (maxspan * CD);
  D = D + (maxspan * CD);

  vtkVector3d A = (vtkVector3d)a;
  vtkVector3d B = (vtkVector3d)b;

  vtkVector3d AB = B - A;
  AB.Normalize();

  A = A - (maxspan * AB);
  B = B + (maxspan * AB);
  vtkSmartPointer<vtkPlane> fixedPlane = this->createPlane(C, D, A, B);
  vtkSmartPointer<vtkPlane> movingPlane = this->createPlane(A, B, C, D);
  this->BendingPlane = fixedPlane;
  this->createBendingLocator();

  C = this->projectToModel(C, fixedPlane);
  D = this->projectToModel(D, fixedPlane);
  A = this->projectToModel(A, movingPlane);
  B = this->projectToModel(B, movingPlane);

  this->SourcePoints->InsertPoint(0, A.GetData());
  this->SourcePoints->InsertPoint(1, B.GetData());
  this->SourcePoints->InsertPoint(2, C.GetData());
  this->SourcePoints->InsertPoint(3, D.GetData());

  //Compute Next point as the vector defining the bend axis
  vtkVector3d E = A + 0.5 * (B - A);
  vtkVector3d CE = E - C;
  CD = D - C;
  vtkVector3d CF = CE.Dot(CD.Normalized()) * CD.Normalized();

  //Midpoint projected onto te line between the fixed points - Pivot point
  vtkVector3d F = C + CF;
  F = projectToModel(F);
  vtkVector3d FE = E - F;
  vtkVector3d FB = B - F;
  vtkVector3d axis = FE.Cross(FB);
  axis.Normalize();

  //Store beding axis in source points
  this->SourcePoints->InsertPoint(4, axis.GetData());
  this->SourcePoints->InsertPoint(5, F.GetData());

  //Agressively downsample to create source points
  vtkNew<vtkCleanPolyData> clean;
  vtkNew<vtkVertexGlyphFilter> verts;
  verts->SetInputData(this->BendingPolyData);
  verts->Update();
  clean->SetInputData(verts->GetOutput());
  clean->SetTolerance(0.07);
  clean->Update();
  this->SourcePointsDense = clean->GetOutput()->GetPoints();
}

//----------------------------------------------------------------------------
//Project a 3D point onto the closest point on the bending model
vtkVector3d vtkSlicerPlannerLogic::projectToModel(vtkVector3d point)
{
  //build locator when model is loaded

  return this->projectToModel(point, this->cellLocator);
}

//----------------------------------------------------------------------------
//Project a 3D point onto the closest point on the bending model, constrained by a plane
vtkVector3d vtkSlicerPlannerLogic::projectToModel(vtkVector3d point, vtkPlane* plane)
{
  vtkNew<vtkCutter> cutter;
  cutter->SetCutFunction(plane);
  cutter->SetInputData(this->ModelToBend->GetPolyData());
  vtkSmartPointer<vtkPolyData> cut;
  cutter->Update();
  cut = cutter->GetOutput();
  return this->projectToModel(point, cut);
}

//----------------------------------------------------------------------------
//Project a 3D point onto the closest point on the specified model
vtkVector3d vtkSlicerPlannerLogic::projectToModel(vtkVector3d point, vtkPolyData* model)
{
  vtkNew<vtkCellLocator> locator;
  vtkNew<vtkTriangleFilter> triangulate;
  triangulate->SetInputData(model);
  triangulate->Update();
  locator->SetDataSet(triangulate->GetOutput());
  locator->BuildLocator();
  return this->projectToModel(point, locator.GetPointer());
}

//----------------------------------------------------------------------------
//Project a 3D point onto the closest point on the model as defined by the provided cell locator
vtkVector3d vtkSlicerPlannerLogic::projectToModel(vtkVector3d point, vtkCellLocator* locator)
{
  double closestPoint[3];//the coordinates of the closest point will be returned here
  double closestPointDist2; //the squared distance to the closest point will be returned here
  vtkIdType cellId; //the cell id of the cell containing the closest point will be returned here
  int subId; //this is rarely used (in triangle strips only, I believe)
  locator->FindClosestPoint(point.GetData(), closestPoint, cellId, subId, closestPointDist2);

  vtkVector3d projection;
  projection.Set(closestPoint[0], closestPoint[1], closestPoint[2]);
  return projection;
}

vtkSmartPointer<vtkPlane> vtkSlicerPlannerLogic::createPlane(vtkVector3d A, vtkVector3d B, vtkVector3d C, vtkVector3d D)
{
  //A and B are in the plane
  //C and D are perp to plane

  vtkSmartPointer<vtkPlane> plane = vtkSmartPointer<vtkPlane>::New();
  vtkVector3d AB = B - A;
  vtkVector3d E = A + 0.5 * AB;
  vtkVector3d CD = D - C;
  plane->SetOrigin(E.GetData());
  plane->SetNormal(CD.GetData());
  return plane;
}

vtkVector3d vtkSlicerPlannerLogic::bendPoint(vtkVector3d point, double magnitude)
{
  double ax[3];
  this->SourcePoints->GetPoint(4, ax);
  vtkVector3d axis = (vtkVector3d)ax;
  vtkVector3d F = projectToModel(point, this->BendingPlaneLocator);
  vtkVector3d AF = F - point;
  vtkVector3d BendingVector;
  if(this->BendingPlane->EvaluateFunction(point.GetData()) < 0)
  {
    BendingVector = AF.Cross(axis);
  }
  else
  {
    BendingVector = axis.Cross(AF);
  }
  vtkVector3d point2 = point + ((magnitude * AF.Norm()) * BendingVector.Normalized());
  vtkVector3d A2F = F - point2;

  //correction factor
  point2 = point2 + (A2F.Norm() - AF.Norm()) * A2F.Normalized();

  return point2;
}

void vtkSlicerPlannerLogic::createBendingLocator()
{
  this->BendingPlaneLocator = vtkSmartPointer<vtkCellLocator>::New();

  vtkNew<vtkCutter> cutter;
  cutter->SetCutFunction(this->BendingPlane);
  cutter->SetInputData(this->BendingPolyData);
  vtkSmartPointer<vtkPolyData> cut;
  cutter->Update();
  cut = cutter->GetOutput();

  vtkNew<vtkTriangleFilter> triangulate;
  triangulate->SetInputData(cut);
  triangulate->Update();
  this->BendingPlaneLocator->SetDataSet(triangulate->GetOutput());
  this->BendingPlaneLocator->BuildLocator();
}

vtkSmartPointer<vtkMatrix4x4> vtkSlicerPlannerLogic::createBendingMatrix(vtkVector3d pointV, double angle)
{
  vtkSmartPointer<vtkMatrix4x4> matrix = vtkSmartPointer<vtkMatrix4x4>::New();
  double axis[3];
  double point[3];
  double L = 1;

  this->SourcePoints->GetPoint(4, axis);
  double u = axis[0];
  double v = axis[1];
  double w = axis[2];
  double u2 = u * u;
  double v2 = v * v;
  double w2 = w * w;
  double a = pointV.GetX();
  double b = pointV.GetY();
  double c = pointV.GetZ();

  matrix->SetElement(0, 0, (u2 + (v2 + w2) * cos(angle)));
  matrix->SetElement(0, 1, (u * v * (1 - cos(angle)) - w * sqrt(L) * sin(angle)));
  matrix->SetElement(0, 2, (u * w * (1 - cos(angle)) + v * sqrt(L) * sin(angle)));
  matrix->SetElement(0, 3, (a * (v2 + w2) - u * (b * v + c * w)) * (1 - cos(angle)) + (b * w - c * v)*sin(angle));

  matrix->SetElement(1, 0, (u * v * (1 - cos(angle)) + w * sqrt(L) * sin(angle)));
  matrix->SetElement(1, 1, (v2 + (u2 + w2) * cos(angle)));
  matrix->SetElement(1, 2, (v * w * (1 - cos(angle)) - u * sqrt(L) * sin(angle)));
  matrix->SetElement(1, 3, (b * (u2 + w2) - v * (a * u + c * w)) * (1 - cos(angle)) + (c * u - a * w)*sin(angle));

  matrix->SetElement(2, 0, (u * w * (1 - cos(angle)) - v * sqrt(L) * sin(angle)));
  matrix->SetElement(2, 1, (v * w * (1 - cos(angle)) + u * sqrt(L) * sin(angle)));
  matrix->SetElement(2, 2, (w2 + (u2 + v2) * cos(angle)));
  matrix->SetElement(2, 3, (c * (u2 + v2) - u * (b * v + c * w)) * (1 - cos(angle)) + (a * v - b * u)*sin(angle));

  matrix->SetElement(3, 0, 0);
  matrix->SetElement(3, 1, 0);
  matrix->SetElement(3, 2, 0);
  matrix->SetElement(3, 3, 1);


  return matrix;
}

vtkVector3d vtkSlicerPlannerLogic::bendPoint2(vtkVector3d point, double angle)
{
  if(this->BendingPlane->EvaluateFunction(point.GetData()) < 0)
  {
    angle = angle;
  }
  else
  {
    angle = -1 * angle;
  }
  vtkVector3d F = this->projectToModel(point, this->BendingPlaneLocator);
  vtkSmartPointer<vtkMatrix4x4> matrixToUse = createBendingMatrix(F, angle);
  double p[4];
  double p_bent[4];
  p[3] = 1;
  p[0] = point.GetX();
  p[1] = point.GetY();
  p[2] = point.GetZ();
  matrixToUse->MultiplyPoint(p, p_bent);
  vtkVector3d bent;
  bent.SetX(p_bent[0] / p_bent[3]);
  bent.SetY(p_bent[1] / p_bent[3]);
  bent.SetZ(p_bent[2] / p_bent[3]);
  return bent;
}

//----------------------------------------------------------------------------
//Remove models and clear data
void vtkSlicerPlannerLogic::clearModelsAndData()
{
  this->clearBendingData();
  if (this->SkullWrappedPreOP)
  {
    this->GetMRMLScene()->RemoveNode(this->SkullWrappedPreOP);
    this->SkullWrappedPreOP = NULL;
  }
  if (this->HealthyBrain)
  {
    this->GetMRMLScene()->RemoveNode(this->HealthyBrain);
    this->HealthyBrain = NULL;
  }
  if (this->CurrentModel)
  {
    this->GetMRMLScene()->RemoveNode(this->CurrentModel);
    this->CurrentModel = NULL;
  }
  if (this->BoneTemplate)
  {
    this->GetMRMLScene()->RemoveNode(this->BoneTemplate);
    this->BoneTemplate = NULL;
  }
  if (this->TempMerged)
  {
    this->GetMRMLScene()->RemoveNode(this->TempMerged);
    this->TempMerged = NULL;
  }

  if (this->TempWrapped)
  {
    this->GetMRMLScene()->RemoveNode(this->TempWrapped);
    this->TempWrapped = NULL;
  }

  this->preOPICV = 0;
  this->healthyBrainICV = 0;
  this->currentICV = 0;
  this->templateICV = 0;

}