//
// Created by Seth Parker on 8/3/15.
//

#include "meshing/itk2vtk.h"


namespace volcart {
    namespace meshing {

        ///// ITK Mesh -> VTK Polydata /////
        itk2vtk::itk2vtk( MeshType::Pointer input, vtkSmartPointer<vtkPolyData> output ) {

            // points + normals
            auto points = vtkSmartPointer<vtkPoints>::New();
            auto pointNormals = vtkSmartPointer<vtkDoubleArray>::New();
            pointNormals->SetNumberOfComponents(3); //3d normals (ie x,y,z)

            for ( PointsInMeshIterator point = input->GetPoints()->Begin(); point != input->GetPoints()->End(); ++point ) {
                // assign the point
                points->InsertPoint(point->Index(), point->Value()[0], point->Value()[1], point->Value()[2]);

                // assign the normal
                PixelType normal;
                if(input->GetPointData(point.Index(), &normal)) {
                    double ptNorm[3] = {normal[0], normal[1], normal[2]};
                    pointNormals->InsertTuple(point->Index(), ptNorm);
                }
            }

            // cells
            auto polys = vtkSmartPointer<vtkCellArray>::New();
            for ( CellIterator cell = input->GetCells()->Begin(); cell != input->GetCells()->End(); ++cell ) {

                auto poly = vtkSmartPointer<vtkIdList>::New();
                for ( PointsInCellIterator point = cell.Value()->PointIdsBegin(); point != cell.Value()->PointIdsEnd(); ++point )
                    poly->InsertNextId(*point);

                polys->InsertNextCell(poly);
            }

            // assign to the mesh
            output->SetPoints(points);
            output->SetPolys(polys);
            if( pointNormals->GetNumberOfTuples() > 0 )
                output->GetPointData()->SetNormals(pointNormals);

        };

        ///// VTK Polydata -> ITK Mesh /////
        vtk2itk::vtk2itk( vtkSmartPointer<vtkPolyData> input, MeshType::Pointer output ) {

            // points + normals
            auto pointNormals = input->GetPointData()->GetNormals();
            for ( vtkIdType p_id = 0; p_id < input->GetNumberOfPoints(); ++p_id ) {

                PointType point = input->GetPoint(p_id);
                output->SetPoint(p_id, point);
                if( pointNormals != nullptr )
                {
                    PixelType normal = pointNormals->GetTuple(p_id);
                    output->SetPointData(p_id, normal);
                }
            }

            // cells
            CellType::CellAutoPointer cell;
            for ( vtkIdType c_id = 0; c_id < input->GetNumberOfCells(); ++c_id ) {

                auto inputCell = input->GetCell(c_id); // input cell
                cell.TakeOwnership( new TriangleType ); // output cell

                for ( vtkIdType p_id = 0; p_id < inputCell->GetNumberOfPoints(); ++p_id ) {
                    cell->SetPointId(p_id, inputCell->GetPointId(p_id)); // assign the point id's
                }

                output->SetCell( c_id, cell );
            }

        };

        ///// ITK Mesh -> ITK QuadEdge Mesh /////
        itk2itkQE::itk2itkQE(MeshType::Pointer input, volcart::QuadMesh::Pointer output ) {

          // Vertices
          volcart::QuadPoint p;
          PixelType n;
          for ( auto point = input->GetPoints()->Begin(); point != input->GetPoints()->End(); ++point ) {
            // Assign the point
            p = point->Value();
            output->SetPoint(point->Index(), p);

            // Assign the normal
            if(input->GetPointData(point->Index(), &n))
                output->SetPointData(point->Index(), n);
          }

          // Faces
          for( auto cell = input->GetCells()->Begin(); cell != input->GetCells()->End(); ++cell ) {
            // Collect the point id's
            std::vector<QuadPointIdentifier> v_ids;
            for( auto point = cell.Value()->PointIdsBegin(); point != cell.Value()->PointIdsEnd(); ++point ) {
              v_ids.push_back(*point);
            }
            //Assign to the mesh
            output->AddFaceTriangle(v_ids[0], v_ids[1], v_ids[2]);
          }
        }

        ///// ITK QuadEdge Mesh -> ITK Mesh /////
        itkQE2itk::itkQE2itk(volcart::QuadMesh::Pointer input, MeshType::Pointer output) {

          // Vertices
          PointType p;
          PixelType n;
          for( auto point = input->GetPoints()->Begin(); point != input->GetPoints()->End(); ++point ) {
            // Assign the point
            p = point->Value();
            output->SetPoint(point->Index(), p);

            // Assign the normal
            if(input->GetPointData(point->Index(), &n))
                output->SetPointData(point->Index(), n);
          }

          // Faces
          CellType::CellAutoPointer cell;
          QuadCellIdentifier id = 0; // QE Meshes use a map so we have to reset their cell ids
          for ( auto c_it = input->GetCells()->Begin(); c_it != input->GetCells()->End(); ++c_it, ++id ) {
            cell.TakeOwnership( new TriangleType ); // output cell
            cell->SetPointIds( c_it->Value()->PointIdsBegin(), c_it->Value()->PointIdsEnd() );

            output->SetCell( id, cell );
          }
        }

    } // namespace meshing
} // namespace volcart
