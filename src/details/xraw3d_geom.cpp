
#ifdef RMESH_USE_SANITY
    #define RMESH_SANITY    x_MemSanity();
#else
    #define RMESH_SANITY
#endif
namespace xraw3d {

//--------------------------------------------------------------------------

void geom::Kill(void)
{
    m_Bone.clear();
    m_Vertex.clear();
    m_Facet.clear();
    m_MaterialInstance.clear();
    m_Mesh.clear();
}

//--------------------------------------------------------------------------

void geom::Serialize
( bool                          isRead
, const char*                   pFileName
, xcore::textfile::file_type    FileType
)
{
    Kill();

    xcore::textfile::stream File;
    xcore::err              Error;

    if (auto Err = File.Open(isRead, pFileName, FileType); Err)
        throw(std::runtime_error(Err.getCode().m_pString));

    if( isRead == false || File.getRecordName() == "Hierarchy" )
    {
        if( File.Record
            ( Error
            , "Hierarchy"
            , [&]( std::size_t& C, xcore::err& Err )
            {
                if(isRead) Err = m_Bone.New( C );
                else       C   = m_Bone.size();
            }
            , [&](std::size_t I, xcore::err& Err )
            {
                int Index = int(I);
                if (Err = File.Field("Index", Index)) return;
                auto& Bone = m_Bone[Index];

                   (Err = File.Field( "Name",       Bone.m_Name )                                                                         )
                || (Err = File.Field( "nChildren",  Bone.m_nChildren )                                                                    )
                || (Err = File.Field( "iParent",    Bone.m_iParent )                                                                      )
                || (Err = File.Field( "Scale",      Bone.m_Scale.m_X,    Bone.m_Scale.m_Y,    Bone.m_Scale.m_Z )                          )
                || (Err = File.Field( "Rotate",     Bone.m_Rotation.m_X, Bone.m_Rotation.m_Y, Bone.m_Rotation.m_Z, Bone.m_Rotation.m_W )  )
                || (Err = File.Field( "Pos",        Bone.m_Position.m_X, Bone.m_Position.m_Y, Bone.m_Position.m_Z )                       )
                ;
            })
        ) throw(std::runtime_error(Error.getCode().m_pString));
    }

    if( isRead == false || File.getRecordName() == "MaterialInstance" )
    {
        if( File.Record
            ( Error
            , "MaterialInstance"
            , [&]( std::size_t& C, xcore::err& Err )
            {
                if(isRead) Err = m_MaterialInstance.New( C );
                else       C   = m_MaterialInstance.size();
            }
            , [&](std::size_t I, xcore::err& Err )
            {
                int Index = int(I);
                if (Err = File.Field("Index", Index)) return;
                auto& MaterialInstance = m_MaterialInstance[Index];

                int nParams = isRead ? 0 : int(MaterialInstance.m_Params.size());

                   (Err = File.Field("Name",        MaterialInstance.m_Name)           )
                || (Err = File.Field("Shader",      MaterialInstance.m_MaterialShader) )
                || (Err = File.Field("Technique",   MaterialInstance.m_Technique)      )
                || (Err = File.Field("nParams",     nParams)                           )
                ;
                if (Err) return;

                // If we are loading then lets preallocate all the parameters
                if( isRead == false )
                {
                    MaterialInstance.m_Params.appendList(nParams);
                }
            })
        ) throw(std::runtime_error(Error.getCode().m_pString));

        //
        // Read all the parameters for each of the instancces
        //
        int iMaterial = 0;
        int iParam    = 0;

        if( File.Record
            ( Error
            , "MaterialInstanceParams"
            , [&]( std::size_t& C, xcore::err& Err )
            {
                if(isRead) Err = m_MaterialInstance.New( C );
                else
                {
                    // For writting we need to determine how many items we need to write
                    C   = [&]{ int nEntries = 0; for (auto& E : m_MaterialInstance) nEntries += int(E.m_Params.size()); return nEntries; }();

                    // If need to write items which is the first item?
                    if(C)
                    {
                        for( int i=0; i< m_MaterialInstance.size<int>(); ++i )
                        {
                            auto& MatI = m_MaterialInstance[i];
                            if( MatI.m_Params.size() )
                            {
                                iMaterial = i;
                                iParam    = 0;
                                break;
                            }
                        }
                    }
                }
            }
            , [&](std::size_t I, xcore::err& Err )
            {
                   ( Err = File.Field("iMaterialInstance", iMaterial) )
                || ( Err = File.Field("iParam",            iParam)    )
                ;
                if(Err) return;

                auto& Param = m_MaterialInstance[iMaterial].m_Params[iParam];

                string TypeName = isRead ? xcore::string::constant{ "" } : material_instance::getTypeString(Param.m_Type);
                xcore::string::view NameView(Param.m_Name.data(), Param.m_Name.size());
                xcore::string::view ValueView(Param.m_Value.data(), Param.m_Value.size());

                   ( Err = File.Field("Name", NameView              ))
                || ( Err = File.Field("Type", TypeName              ))
                || ( Err = File.Field("Value", ValueView            ))
                ;
                if (Err) return;

                if( isRead )
                {
                    // Set the type of the param
                    Param.m_Type = material_instance::params_type::INVALID;
                    for( int i=1, end_i = int(material_instance::params_type::ENUM_COUNT); i < end_i; ++i )
                    {
                        const auto e = static_cast<material_instance::params_type>(i);
                        if( xcore::string::CompareI( &TypeName[i], material_instance::getTypeString(e) ) == 0 )
                        {
                            Param.m_Type = e;
                            break;
                        }
                    }

                    if( Param.m_Type == material_instance::params_type::INVALID )
                        throw(std::runtime_error("One of the parameters in the material instance was invalid"));
                }
                else
                {
                    iParam++;
                    if(iParam == int(m_MaterialInstance[iMaterial].m_Params.size()) )
                    {
                        while (m_MaterialInstance[++iMaterial].m_Params.size() == 0);
                    }
                    
                }
            })
         ) throw(std::runtime_error(Error.getCode().m_pString));

    } // material instances and params
    

    int nBTNs      = 0;
    int nUVSets    = 0;
    int nColors    = 0;
    int nWeights   = 0;

    if( File.Record
        ( Error
        , "Vertices"
        , [&]( std::size_t& C, xcore::err& Err )
        {
            if(isRead) 
            {
                if( Err = m_Vertex.Alloc( C ) ) return;
                std::memset( m_Vertex.data(), 0, m_Vertex.getByteSize() );
            }
            else       C   = m_Vertex.size();
        }
        , [&](std::size_t I, xcore::err& Err )
        {
            int Index = int(I);
            if (Err = File.Field("Index", Index)) return;

            auto& Vertex = m_Vertex[Index];

               ( Err = File.Field("Pos",        Vertex.m_Position.m_X, Vertex.m_Position.m_Y, Vertex.m_Position.m_Z) )
            || ( Err = File.Field("nBinormals", Vertex.m_nBinormals)                                                 )
            || ( Err = File.Field("nTangents",  Vertex.m_nTangents)                                                  )
            || ( Err = File.Field("nNormals",   Vertex.m_nNormals)                                                   )
            || ( Err = File.Field("nUVSets",    Vertex.m_nUVs)                                                       )
            || ( Err = File.Field("nColors",    Vertex.m_nColors)                                                    )
            || ( Err = File.Field("nWeights",   Vertex.m_nWeights)                                                   )
            ;
            if(Err) return;

           nBTNs      += std::max( Vertex.m_nTangents, std::max( Vertex.m_nNormals, Vertex.m_nTangents) );
           nUVSets    += Vertex.m_nUVs     ;
           nColors    += Vertex.m_nColors  ;
           nWeights   += Vertex.m_nWeights ;
        })
     ) throw(std::runtime_error(Error.getCode().m_pString));
    

    if( isRead == false || File.getRecordName() == "Colors" )
    {
        int iColor  = 0;
        int iVertex = 0;

        if( File.Record
            ( Error
            , "Colors"
            , [&]( std::size_t& C, xcore::err& Err )
            {
                if(isRead) xassert( C == nColors );
                else       C   = nColors;
            }
            , [&](std::size_t, xcore::err& Err )
            {
                // Skip writing vertices that don't have colors
                if( isRead == false )
                {
                    while( m_Vertex[iVertex].m_nColors == 0 )
                    {
                        iVertex++;
                        iColor = 0;
                    }
                }

                if (Err = File.Field("iVertex", iVertex)) return;
                auto& Vertex = m_Vertex[iVertex];

                if (Err = File.Field("Index", iColor)) return;
                auto& C = Vertex.m_Color[++iColor];
                if( iColor == Vertex.m_nColors ) 
                {
                    iVertex++;
                    iColor  = 0;
                }

                xcore::vector4 FColor = C.getRGBA();
                if( Err = File.Field("Color", FColor.m_X, FColor.m_Y, FColor.m_Z, FColor.m_W) ) return;
                if( isRead ) C.setupFromRGBA( FColor );
            })
         ) throw(std::runtime_error(Error.getCode().m_pString));
    }
    
    if( isRead == false || File.getRecordName() == "BTNs" )
    {
        int iBTN    = 0;
        int iVertex = 0;

        if( File.Record
            ( Error
            , "BTNs"
            , [&]( std::size_t& C, xcore::err& Err )
            {
                if(isRead) xassert( C == nBTNs);
                else       C   = nBTNs;
            }
            , [&](std::size_t, xcore::err& Err )
            {
                // Skip writing vertices that don't have btns
                if (isRead == false)
                {
                    while( m_Vertex[iVertex].m_nBinormals == 0 
                        && m_Vertex[iVertex].m_nTangents  == 0
                        && m_Vertex[iVertex].m_nNormals   == 0 )
                    {
                        iVertex++;
                        iBTN = 0;
                    }
                }

                if (Err = File.Field("iVertex", iVertex)) return;
                auto& Vertex = m_Vertex[iVertex];

                if (Err = File.Field("Index", iBTN)) return;
                auto& BTN = Vertex.m_BTN[iBTN];
                if( iBTN >= Vertex.m_nTangents 
                 && iBTN >= Vertex.m_nNormals
                 && iBTN >= Vertex.m_nBinormals )
                {
                    iVertex++;
                    iBTN = 0;
                }

                   (Err = File.Field("Binormals", BTN.m_Binormal.m_X, BTN.m_Binormal.m_Y, BTN.m_Binormal.m_Z, BTN.m_Binormal.m_W ))
                || (Err = File.Field("Tangents",  BTN.m_Tangent.m_X,  BTN.m_Tangent.m_Y,  BTN.m_Tangent.m_Z,  BTN.m_Tangent.m_W ))
                || (Err = File.Field("Normals",   BTN.m_Normal.m_X,   BTN.m_Normal.m_Y,   BTN.m_Normal.m_Z))
                ;
            })
         ) throw(std::runtime_error(Error.getCode().m_pString));
    }


    if( isRead == false || File.getRecordName() == "UVs" )
    {
        int iUVs    = 0;
        int iVertex = 0;

        if( File.Record
            ( Error
            , "UVs"
            , [&]( std::size_t& C, xcore::err& Err )
            {
                if(isRead) xassert( C == nUVSets );
                else       C   = nUVSets;
            }
            , [&](std::size_t, xcore::err& Err )
            {
                // Skip writing vertices that don't have colors
                if( isRead == false )
                {
                    while( m_Vertex[iVertex].m_nUVs == 0 )
                    {
                        iVertex++;
                        iUVs = 0;
                    }
                }

                if (Err = File.Field("iVertex", iVertex)) return;
                auto& Vertex = m_Vertex[iVertex];

                if (Err = File.Field("Index", iUVs)) return;
                auto& UV = Vertex.m_UV[++iUVs];
                if(iUVs == Vertex.m_nUVs )
                {
                    iVertex++;
                    iUVs = 0;
                }

                Err = File.Field("UV", UV.m_X, UV.m_Y );
            })
         ) throw(std::runtime_error(Error.getCode().m_pString));
    }

    if( isRead == false || File.getRecordName() == "Skin" )
    {
        int iWeight = 0;
        int iVertex = 0;

        if( File.Record
            ( Error
            , "Skin"
            , [&]( std::size_t& C, xcore::err& Err )
            {
                if(isRead) xassert( C == nWeights);
                else       C   = nWeights;
            }
            , [&](std::size_t, xcore::err& Err )
            {
                // Skip writing vertices that don't have colors
                if( isRead == false )
                {
                    while( m_Vertex[iVertex].m_nWeights == 0 )
                    {
                        iVertex++;
                        iWeight = 0;
                    }
                }

                if (Err = File.Field("iVertex", iVertex)) return;
                auto& Vertex = m_Vertex[iVertex];

                if (Err = File.Field("Index", iWeight)) return;
                auto& Weight = Vertex.m_Weight[++iWeight];
                if(iWeight == Vertex.m_nWeights )
                {
                    iVertex++;
                    iWeight = 0;
                }

                   ( Err =  File.Field("iBone",  Weight.m_iBone) )
                || ( Err =  File.Field("Weight", Weight.m_Weight))
                ;
            })
         ) throw(std::runtime_error(Error.getCode().m_pString));
    }

    int nIndices = 0;
    if( File.Record
        ( Error
        , "Polygons"
        , [&]( std::size_t& C, xcore::err& Err )
        {
            if(isRead) 
            {
                if( Err = m_Facet.Alloc( C ) ) return;
                std::memset(m_Facet.data(), 0, m_Facet.getByteSize());
            }
            else       C   = m_Facet.size<int>();
        }
        , [&](std::size_t I, xcore::err& Err )
        {
            auto& Facet = m_Facet[I];

               ( Err =  File.Field("iMesh",     Facet.m_iMesh)              )
            || ( Err =  File.Field("nVerts",    Facet.m_nVertices)          )
            || ( Err =  File.Field("Plane",     Facet.m_Plane.m_Normal.m_X  
                                              , Facet.m_Plane.m_Normal.m_Y  
                                              , Facet.m_Plane.m_Normal.m_Z  
                                              , Facet.m_Plane.m_D )         )
            || ( Err =  File.Field("iMaterialInstance", Facet.m_iMaterialInstance)  )
            ;

            if( !Err ) nIndices += Facet.m_nVertices;
        })
     ) throw(std::runtime_error(Error.getCode().m_pString));

    {
        int iIndex = 0;
        int iFacet = 0;
        if( File.Record
            ( Error
            , "FacetIndex"
            , [&]( std::size_t& C, xcore::err& Err )
            {
                if(isRead) xassert( C == nIndices );
                else       C   = nIndices;
            }
            , [&](std::size_t, xcore::err& Err )
            {
                   (Err = File.Field("iFacet", iFacet)      )
                || (Err = File.Field("Index",  iIndex)      )
                ;
                if( Err ) return;

                auto& Facet = m_Facet[iFacet];
                auto& Index = Facet.m_iVertex[++iIndex];
                if( iIndex == Facet.m_nVertices )
                {
                    iFacet++;
                    iIndex = 0;
                }

                Err = File.Field("iVertex", Index );
            })
         ) throw(std::runtime_error(Error.getCode().m_pString));
    }

    if (isRead == false || File.getRecordName() == "Mesh")
    {
        if( File.Record
            ( Error
            , "Mesh"
            , [&]( std::size_t& C, xcore::err& Err )
            {
                if(isRead) Err = m_Mesh.New( C );
                else       C   = nIndices;
            }
            , [&](std::size_t I, xcore::err& Err )
            {
                Err = File.Field("Name", m_Mesh[I].m_Name);
            })
         ) throw(std::runtime_error(Error.getCode().m_pString));

        if (isRead)
        {
            // Rename duplicated names if we found any
            for( mesh& Mesh : m_Mesh )
            {
                int Count = 0;
                for( int j =  1 + m_Mesh.getIndexByEntry<int>(Mesh); j < m_Mesh.size<int>(); j++ )
                {
                    if ( xcore::string::CompareI( Mesh.m_Name, m_Mesh[j].m_Name ) == 0 )
                    {
                         m_Mesh[j].m_Name = xcore::string::Fmt( "%s__%d", (const char*)m_Mesh[j].m_Name, Count++ );
                    }
                }
            }
        }
    }
        
    // Compute all bone related info
    if( isRead ) ComputeBoneInfo();
}

//--------------------------------------------------------------------------

xcore::bbox geom::getBBox( void )
{
    xcore::bbox BBox;
    BBox.setZero();
    for( std::int32_t i=0; i<m_Vertex.size<int>(); i++ )
    {
        BBox += m_Vertex[i].m_Position;
    }
    return BBox;
}

//--------------------------------------------------------------------------

void geom::SortFacetsByMaterial( void )
{
    std::qsort( &m_Facet[0], m_Facet.size<int>(), sizeof(facet),
            [](const void* paA, const void* paB ) -> std::int32_t
            {
                const geom::facet* pA = (const geom::facet*)paA;
                const geom::facet* pB = (const geom::facet*)paB;
                
                if( pA->m_iMesh < pB->m_iMesh ) return -1;
                if( pA->m_iMesh > pB->m_iMesh ) return  1;
                
                if( pA->m_iMaterialInstance < pB->m_iMaterialInstance ) return -1;
                return pA->m_iMaterialInstance > pB->m_iMaterialInstance;
            });
}

//--------------------------------------------------------------------------

void geom::SortFacetsByMeshMaterialBone( void )
{
   static geom* g_pCompare;
    
   g_pCompare = this;
    
   std::qsort( &m_Facet[0], m_Facet.size<int>(), sizeof(facet),
            [](const void* paA, const void* paB ) -> std::int32_t
            {
                const geom::facet* pA = (const geom::facet*)paA;
                const geom::facet* pB = (const geom::facet*)paB;
                
                if( pA->m_iMesh < pB->m_iMesh ) return -1;
                if( pA->m_iMesh > pB->m_iMesh ) return  1;
                
                if( pA->m_iMaterialInstance < pB->m_iMaterialInstance ) return -1;
                
                if( pA->m_iMaterialInstance > pB->m_iMaterialInstance ) return 1;
                
                if( pA->m_iMaterialInstance == pB->m_iMaterialInstance )
                {
                    if( g_pCompare->m_Vertex[pA->m_iVertex[0]].m_Weight[0].m_iBone >
                        g_pCompare->m_Vertex[pB->m_iVertex[0]].m_Weight[0].m_iBone )
                        return 1;
                    
                    if( g_pCompare->m_Vertex[pA->m_iVertex[0]].m_Weight[0].m_iBone <
                        g_pCompare->m_Vertex[pB->m_iVertex[0]].m_Weight[0].m_iBone )
                        return -1;
                }
                return 0;
            });
}

//--------------------------------------------------------------------------

bool geom::TempVCompare( const vertex& A, const vertex& B )
{
    std::int32_t i;

    // Check position first
    {
        static const float PEpsilon = 0.001f; 
        xcore::vector3 T;
        T = A.m_Position - B.m_Position;
        float d = T.Dot( T );
        if( d > PEpsilon ) return false;
    }
    
    if( A.m_nWeights != B.m_nWeights ) return false;
    for( i=0; i<A.m_nWeights; i++ )
    {
        static const float WEpsilon = 0.001f;
        float d = (A.m_Weight[i].m_Weight*A.m_Weight[i].m_Weight) - (B.m_Weight[i].m_Weight*B.m_Weight[i].m_Weight);
        if( d > WEpsilon ) return false;
        if( A.m_Weight[i].m_iBone != B.m_Weight[i].m_iBone ) return false;
    }

    if( A.m_nNormals != B.m_nNormals ) return false;
    for( i=0; i<A.m_nNormals; i++ )
    {
        static const float NEpsilon = 0.001f;
        xcore::vector3 T;
        T = B.m_BTN[ i ].m_Normal - A.m_BTN[ i ].m_Normal;
        float d = T.Dot( T );
        if( d > NEpsilon ) return false;
    }

    if( A.m_nTangents != B.m_nTangents ) return false;
    for( i=0; i<A.m_nTangents; i++ )
    {
        static const float NEpsilon = 0.001f;
        xcore::vector3 T;
        T = B.m_BTN[ i ].m_Tangent - A.m_BTN[ i ].m_Tangent;
        float d = T.Dot( T );
        if( d > NEpsilon ) return false;
    }

    if( A.m_nBinormals != B.m_nBinormals ) return false;
    for( i=0; i<A.m_nBinormals; i++ )
    {
        static const float NEpsilon = 0.001f;
        xcore::vector3 T;
        T = B.m_BTN[ i ].m_Binormal - A.m_BTN[ i ].m_Binormal;
        float d = T.Dot( T );
        if( d > NEpsilon ) return false;
    }

    if( A.m_nUVs != B.m_nUVs ) return false;
    for( i=0; i<A.m_nUVs; i++ )
    {
        static const float UVEpsilon = 0.001f;
        xcore::vector2 T;
        T = B.m_UV[i] - A.m_UV[i];
        float d = T.Dot( T );
        if( d > UVEpsilon ) return false;
    }

    if( A.m_nColors != B.m_nColors ) return false;
    for( i=0; i<A.m_nColors; i++ )
    {
        static const float CEpsilon = 3.0f;
        xcore::vector4 C1( A.m_Color[i].m_R, A.m_Color[i].m_G, A.m_Color[i].m_B, A.m_Color[i].m_A );
        xcore::vector4 C2( B.m_Color[i].m_R, B.m_Color[i].m_G, B.m_Color[i].m_B, B.m_Color[i].m_A );

        xcore::vector4 T = C1 - C2;
        float d = T.Dot( T );
        if( d > CEpsilon ) return false;
    }

    return true;
}

//--------------------------------------------------------------------------

bool geom::CompareFaces( const geom::facet& A, const geom::facet& B )
{
    std::int32_t i;

    if( A.m_iMesh     != B.m_iMesh     ) return false;
    if( A.m_nVertices != B.m_nVertices ) return false;
    if( A.m_iMaterialInstance != B.m_iMaterialInstance ) return false;

    for( i=0; i<A.m_nVertices; i++ )
    {
        if( A.m_iVertex[i] == B.m_iVertex[0] )
            break;
    }
    if( i == A.m_nVertices ) return false;

    std::int32_t Index = i;
    for( i=0; i<B.m_nVertices; i++ )
    {
        if( B.m_iVertex[i] != A.m_iVertex[(i+Index)%A.m_nVertices] )
            return false;
    }

    return true;
}

//--------------------------------------------------------------------------

void geom::ForceAddColorIfNone(void)
{
    for( auto& V : m_Vertex )
    {
        if( V.m_nColors == 0 )
        {
            V.m_nColors = 1;
            V.m_Color[0].m_Value = ~0u;
        }
    }
}

//--------------------------------------------------------------------------

void geom::CleanMesh( std::int32_t iMesh /* = -1 */ ) // Remove this Mesh
{
    std::int32_t TotalFacetsRemoved = 0;
    std::int32_t TotalVerticesRemoved = 0;
    std::int32_t TotalMaterialsRemoved = 0;

    RMESH_SANITY

    //
    // Make sure that all normals are normalied
    //
    {
        for ( std::int32_t i = 0; i < m_Vertex.size<int>(); i++ )
        {
            auto& Vertex = m_Vertex[i];

            if( Vertex.m_nBinormals != Vertex.m_nTangents )
                throw(std::runtime_error("ERROR: The mesh has a different number of Binormals To Tangents"));

            if( Vertex.m_nNormals < Vertex.m_nBinormals )
                throw(std::runtime_error( "ERROR: We have more Binormals than Normals" ));

            for ( std::int32_t j = 0; j < Vertex.m_nNormals; j++ )
            {
                Vertex.m_BTN[j].m_Normal.NormalizeSafe();
                if( j < Vertex.m_nBinormals )
                {
                    Vertex.m_BTN[ j ].m_Binormal.NormalizeSafe();
                    Vertex.m_BTN[ j ].m_Tangent.NormalizeSafe();
                }
            }
        }
    }
    //
    // Sort weights from largest to smallest
    //
    {
        std::int32_t i, j, k;
        for ( i = 0; i < m_Vertex.size<int>(); i++ )
        {
            for ( j = 0; j < m_Vertex[ i ].m_nWeights; j++ )
            {
                std::int32_t BestW = j;
                for ( k = j + 1; k<m_Vertex[ i ].m_nWeights; k++ )
                {
                    if ( m_Vertex[ i ].m_Weight[ k ].m_Weight > m_Vertex[ i ].m_Weight[ BestW ].m_Weight )
                        BestW = k;
                }

                weight TW = m_Vertex[ i ].m_Weight[ j ];
                m_Vertex[ i ].m_Weight[ j ] = m_Vertex[ i ].m_Weight[ BestW ];
                m_Vertex[ i ].m_Weight[ BestW ] = TW;
            }
        }
    }

    RMESH_SANITY

    //
    // Collapse vertices that are too close from each other and have 
    // the same properties.
    //
    if (1)
    {
        struct hash
        {
            std::int32_t     m_iVRemap;
            std::int32_t     m_iNext;
        };

        struct tempv
        {
            std::int32_t     m_RemapIndex;     // Which vertex Index it shold now use.
            std::int32_t     m_Index;          // Inde to the original vertex
            std::int32_t     m_iNext;          // next node in the has
        };

        if ( m_Vertex.size<int>() <= 0 )
            throw(std::runtime_error( "geom has not vertices" ));

        std::int32_t                i;
        xcore::unique_span<hash>    Hash;
        xcore::unique_span<tempv>   TempV;
        const std::int32_t          HashDimension  = std::max( 20, std::int32_t(std::sqrtf((float)m_Vertex.size<int>())) );
        const std::int32_t          HashSize       = HashDimension * HashDimension;
        float                       MaxX, MinX, XShift;
        float                       MaxZ, MinZ, ZShift;

        // Allocate memory
        if( auto Err = Hash.New( HashSize ); Err ) throw(std::runtime_error(Err.getCode().m_pString));
        if (auto Err = TempV.New( m_Vertex.size<int>() ); Err) throw(std::runtime_error(Err.getCode().m_pString));

        // Initialize the hash with terminators
        for ( i = 0; i < HashSize; i++ )
        {
            Hash[ i ].m_iNext = -1;
        }

        // Fill the nodes for each of the dimensions
        MaxX = m_Vertex[ 0 ].m_Position.m_X;
        MaxZ = m_Vertex[ 0 ].m_Position.m_Z;
        MinX = MaxX;
        MinZ = MaxZ;
        {
            std::int32_t TotalCrazyVerts = 0;
            for ( i = 0; i < m_Vertex.size<int>(); i++ )
            {
                static const float CrazyMax = 10000.f;

                TempV[ i ].m_Index = i;
                TempV[ i ].m_iNext = -1;
                TempV[ i ].m_RemapIndex = i;

                //
                // Watch out for crazy verts
                //
                const float XDisMin = std::abs( MaxX - m_Vertex[ i ].m_Position.m_X );
                const float XDisMax = std::abs( m_Vertex[ i ].m_Position.m_X-MinX   );

                if ( XDisMin > CrazyMax || XDisMax > CrazyMax )
                {
                    TotalCrazyVerts++;
                    continue;
                }

                const float ZDisMin = std::abs( MaxZ - m_Vertex[ i ].m_Position.m_Z );
                const float ZDisMax = std::abs( m_Vertex[ i ].m_Position.m_Z-MinZ   );

                if ( ZDisMin > CrazyMax || ZDisMax > CrazyMax )
                {
                    TotalCrazyVerts++;
                    continue;
                }

                //
                // Get the max
                //
                MaxX = std::max( MaxX, m_Vertex[ i ].m_Position.m_X );
                MinX = std::min( MinX, m_Vertex[ i ].m_Position.m_X );
                MaxZ = std::max( MaxZ, m_Vertex[ i ].m_Position.m_Z );
                MinZ = std::min( MinZ, m_Vertex[ i ].m_Position.m_Z );
            }

            if ( TotalCrazyVerts > 5000 )
                throw(std::runtime_error( "ERROR: We have too many vertices that are outside an acceptable range" ));

        }

        // Hash all the vertices into the hash table
        XShift = ( HashDimension - 1 ) / ( (MaxX - MinX) + 1 );
        ZShift = ( HashDimension - 1 ) / ( (MaxZ - MinZ) + 1 );
        for ( i = 0; i < m_Vertex.size<int>(); i++ )
        {
            const std::int32_t XOffSet = (std::int32_t)std::clamp( ( ( m_Vertex[ i ].m_Position.m_X - MinX ) * XShift ), 0.f, (float)HashDimension );
            const std::int32_t ZOffSet = (std::int32_t)std::clamp( ( ( m_Vertex[ i ].m_Position.m_Z - MinZ ) * ZShift ), 0.f, (float)HashDimension );

            xassert( XOffSet >= 0 );
            xassert( XOffSet < HashDimension );
            xassert( ZOffSet >= 0 );
            xassert( ZOffSet < HashDimension );

            const std::int32_t   iEntry      = XOffSet + HashDimension * ZOffSet;
            hash&       HashEntry   = Hash[ iEntry ];

            TempV[ i ].m_iNext      = HashEntry.m_iNext;
            HashEntry.m_iNext       = i;
        }

        xcore::scheduler::channel BlockJobs( xconst_universal_str("CleanMesh") );

        // Now do a seach for each vertex
        for ( std::int32_t i = 0; i < HashSize; i++ )
        {
            const std::int32_t XCell     = (i%HashDimension);
            const std::int32_t ZCell     = (i/HashDimension);
            const std::int32_t XFrom     = std::max( 0, XCell );
            const std::int32_t ZFrom     = std::max( 0, ZCell );
            const std::int32_t XTo       = std::min( HashDimension-1, XCell + 1);
            const std::int32_t ZTo       = std::min( HashDimension-1, ZCell + 1);

            for ( std::int32_t k = Hash[ i ].m_iNext; k != -1; k = TempV[ k ].m_iNext )
            {
                const tempv& TempVKeyEntry = TempV[ k ];

                // This vertex has been remap
                if ( TempVKeyEntry.m_RemapIndex != TempVKeyEntry.m_Index )
                    continue;

                for ( std::int32_t x=XFrom; x!=XTo; ++x )
                for ( std::int32_t z=ZFrom; z!=ZTo; ++z )
                {
                    const std::int32_t   iHash       = x + z * HashDimension;
                    const hash& ExploreHash = Hash[ iHash ];
                    const bool bSameHash   = iHash == i;

                    BlockJobs.SubmitJob( [this, &ExploreHash, &TempVKeyEntry, k, &TempV, bSameHash ]()
                    {
                        std::int32_t TotalEntryPerCell = 0;
                        std::int32_t iStartNode;

                        if ( bSameHash ) 
                            iStartNode = TempV[ k ].m_iNext;
                        else
                            iStartNode = ExploreHash.m_iNext;

                        // Seach all the nodes inside this hash
                        for ( std::int32_t j = iStartNode; j != -1; j = TempV[ j ].m_iNext )
                        {
                            tempv& VEntryTest = TempV[ j ];

                            TotalEntryPerCell++;

                            xassert ( &VEntryTest != &TempVKeyEntry );

                            // This vertex has been remap
                            if ( VEntryTest.m_RemapIndex != VEntryTest.m_Index )
                                continue;

                            // If both vertices are close then remap vertex
                            if ( TempVCompare( m_Vertex[ TempVKeyEntry.m_RemapIndex ], m_Vertex[ VEntryTest.m_Index ] ) )
                                VEntryTest.m_RemapIndex = TempVKeyEntry.m_RemapIndex;
                        }

                        xassert( TotalEntryPerCell < (m_Vertex.size<int>()/4) );
                    });
                }

                BlockJobs.join();
            }
        }
        RMESH_SANITY

        // Okay now we must collapse all the unuse vertices
        std::int32_t nVerts = 0;
        for ( i = 0; i < m_Vertex.size<int>(); i++ )
        {
            if ( TempV[ i ].m_RemapIndex == TempV[ i ].m_Index )
            {
                TempV[ i ].m_RemapIndex = nVerts;
                TempV[ i ].m_Index      = -1;      // Mark as we have cranch it
                nVerts++;
            }
        }

        RMESH_SANITY

        // OKay now get all the facets and remap their indices
        for ( i = 0; i < m_Facet.size<int>(); i++ )
        for ( std::int32_t j = 0; j < m_Facet[ i ].m_nVertices; j++ )
        {
            std::int32_t&    iVert  = m_Facet[ i ].m_iVertex[ j ];
            std::int32_t     iRemap = TempV[ iVert ].m_RemapIndex;

            if ( TempV[ iVert ].m_Index == -1 )
            {
                iVert = iRemap;
            }
            else
            {
                iVert = TempV[ iRemap ].m_RemapIndex;
                xassert( TempV[ iRemap ].m_Index == -1 );
            }
        }

        RMESH_SANITY

        // Now copy the vertices to their final location
        xcore::unique_span<vertex>    Vertex;
        if( auto Err = Vertex.Alloc( nVerts ); Err ) throw(std::runtime_error(Err.getCode().m_pString));

        for ( i = 0; i < m_Vertex.size<int>(); i++ )
        {
            std::int32_t iRemap = TempV[ i ].m_RemapIndex;

            if ( TempV[ i ].m_Index == -1 )
            {
                Vertex[ iRemap ] = m_Vertex[ i ];
            }
            /*
            else
            {
                Vertex[ TempV[ iRemap ].m_RemapIndex ] = m_Vertex[ i ];
            }
            */
        }

        RMESH_SANITY

        // Finally set the new count and
        TotalVerticesRemoved += m_Vertex.size<int>() - nVerts;
        m_Vertex = std::move(Vertex);

        RMESH_SANITY
    }

    RMESH_SANITY

        //
        // Elliminate any digenerated facets
        //
    {
        std::int32_t i;
        std::int32_t nFacets = 0;

        for ( i = 0; i < m_Facet.size<int>(); i++ )
        {
            xcore::vector3 Normal = m_Vertex[ m_Facet[ i ].m_iVertex[ 1 ] ].m_Position - m_Vertex[ m_Facet[ i ].m_iVertex[ 0 ] ].m_Position.Cross(
                m_Vertex[ m_Facet[ i ].m_iVertex[ 2 ] ].m_Position - m_Vertex[ m_Facet[ i ].m_iVertex[ 0 ] ].m_Position );

            // Remove this facet if we're dumping out this Mesh.
            if ( ( iMesh != -1 && m_Facet[ i ].m_iMesh == iMesh )
                || Normal.getLength() < 0.00001f )
            {
                // Skip Facet
                //x_DebugMsg("Removing face %1d, (%1d,%1d,%1d)\n",i,m_pFacet[i].iVertex[0],m_pFacet[i].iVertex[1],m_pFacet[i].iVertex[2]);
            }
            else
            {
                m_Facet[ nFacets ] = m_Facet[ i ];
                nFacets++;
            }
        }

        // Set the new count
        TotalFacetsRemoved += m_Facet.size<int>() - nFacets;

        if ( TotalFacetsRemoved )
            if( auto Err = m_Facet.resize( nFacets ); Err ) throw(std::runtime_error(Err.getCode().m_pString));

        // No facets left!
        if ( m_Facet.size<int>() <= 0 )
            throw(std::runtime_error( "geom has not facets" ));
    }

    RMESH_SANITY

        //
        // Elliminate any unuse vertices
        //
    REMOVE_VERTS_AGAIN :
    {
        std::int32_t     i, j;

        if ( m_Vertex.size<int>() <= 0 )
            throw(std::runtime_error( "geom has no vertices" ));

        // Allocat the remap table
        xcore::unique_span<std::int32_t> VRemap;

        if( auto Err = VRemap.Alloc( m_Vertex.size<int>() ); Err ) throw(std::runtime_error(Err.getCode().m_pString));

        // Fill the remap table
        for ( i = 0; i < m_Vertex.size<int>(); i++ )
        {
            VRemap[ i ] = -1;
        }

        // Mark all the used vertices
        for ( facet& Face : m_Facet )
        for ( std::int32_t j = 0; j < Face.m_nVertices; j++ )
        {
            if ( Face.m_iVertex[ j ] < 0 ||
                Face.m_iVertex[ j ] >= m_Vertex.size<int>() )
                throw(std::runtime_error( std::format( "Found a facet that was indexing a vertex out of range! FaceID = {} VertexID = {}",
                i, Face.m_iVertex[ j ] )));

            VRemap[ Face.m_iVertex[ j ] ] = -2;
        }

        // Create the remap table
        // and compact the vertices to the new location
        for ( j = i = 0; i < m_Vertex.size<int>(); i++ )
        {
            std::int32_t Value = VRemap[ i ];

            VRemap[ i ] = j;
            m_Vertex[ j ] = m_Vertex[ i ];

            if ( Value == -2 ) j++;
        }

        // Set the final vertex count
        TotalVerticesRemoved += m_Vertex.size<int>() - j;
        if ( TotalVerticesRemoved )
            if( auto Err = m_Vertex.resize( j ); Err ) throw(std::runtime_error(Err.getCode().m_pString));

        // Remap all the faces to point to the new location of verts
        for ( facet& Face : m_Facet )
        for ( std::int32_t j = 0; j < Face.m_nVertices; j++ )
        {
            Face.m_iVertex[ j ] = VRemap[ Face.m_iVertex[ j ] ];
        }
    }

    RMESH_SANITY


    //
    // Nuke any facets that has the same vert indices and properties
    //
    {
        struct fref
        {
            std::int32_t m_iFacet;
            std::int32_t m_iNext;
        };

        std::int32_t         i;
        std::int32_t         nFacets;
        xcore::unique_span<std::int32_t>   VNode;
        xcore::unique_span<fref>  FRef;
        std::int32_t         nRefs;
        std::int32_t         iRef;

        // Make sure that we have vertices
        if ( m_Vertex.size<int>() <= 0 )
            throw(std::runtime_error( "geom has not vertices" ));

        // Get how many ref we should have
        nRefs = 0;
        for ( i = 0; i < m_Facet.size<int>(); i++ )
        {
            nRefs += m_Facet[ i ].m_nVertices;
        }

        // Allocate hash, and refs
        if( auto Err = VNode.New( m_Vertex.size<int>() ); Err ) throw(std::runtime_error(Err.getCode().m_pString));
        if( auto Err = FRef.New( nRefs ); Err ) throw(std::runtime_error(Err.getCode().m_pString));

        // Initalize the hash entries to null
        for ( i = 0; i < m_Vertex.size<int>(); i++ )
        {
            VNode[ i ] = -1;
        }

        // Insert all the face references into the hash
        iRef = 0;
        for ( i = 0; i < m_Facet.size<int>(); i++ )
        for ( std::int32_t j = 0; j < m_Facet[ i ].m_nVertices; j++ )
        {
            xassert( iRef < nRefs );
            FRef[ iRef ].m_iFacet = i;
            FRef[ iRef ].m_iNext = VNode[ m_Facet[ i ].m_iVertex[ j ] ];
            VNode[ m_Facet[ i ].m_iVertex[ j ] ] = iRef;
            iRef++;
        }

        // Find duplicate facets
        for ( i = 0; i < m_Vertex.size<int>(); i++ )
        for ( std::int32_t j = VNode[ i ]; j != -1; j = FRef[ j ].m_iNext )
        {
            facet& A = m_Facet[ FRef[ j ].m_iFacet ];

            // This facet has been removed
            if ( A.m_nVertices < 0 )
                continue;

            for ( std::int32_t k = FRef[ j ].m_iNext; k != -1; k = FRef[ k ].m_iNext )
            {
                facet& B = m_Facet[ FRef[ k ].m_iFacet ];

                // This facet has been removed
                if ( B.m_nVertices < 0 )
                    continue;

                // Check whether the two facets are the same
                if ( CompareFaces( A, B ) )
                {
                    // Mark for removal
                    B.m_nVertices = 0;
                }
            }
        }

        // Remove any unwanted facets
        nFacets = 0;
        for ( i = 0; i < m_Facet.size<int>(); i++ )
        {
            if ( m_Facet[ i ].m_nVertices == 0 )
            {
                // Skip Facet
            }
            else
            {
                m_Facet[ nFacets ] = m_Facet[ i ];
                nFacets++;
            }
        }

        // Set the new count
        const std::int32_t nFacesRemoved = m_Facet.size<int>() - nFacets;
        TotalFacetsRemoved += nFacesRemoved;
        if ( TotalFacetsRemoved )
            if( auto Err = m_Facet.resize( nFacets ); Err ) throw(std::runtime_error(Err.getCode().m_pString));

        // No facets left!
        if ( m_Facet.size<int>() <= 0 )
            throw(std::runtime_error( "geom has not facets" ));

        if ( nFacesRemoved > 0 )
            goto REMOVE_VERTS_AGAIN;
    }


    RMESH_SANITY

    //
    // Remove materials that are not been use
    //
    if( m_MaterialInstance.size<int>() > 0 )
    {
        struct mat
        {
            bool   m_Used;
            std::int32_t     m_ReIndex;
        };

        std::int32_t         i;
        xcore::unique_span<mat>   Used;
        
        if( auto Err = Used.New( m_MaterialInstance.size<int>( ) );Err ) throw(std::runtime_error(Err.getCode().m_pString));

        for ( mat& Mat : Used )
        {
            Mat.m_ReIndex = -1;
            Mat.m_Used    = false;
        }
        
        // Go throw the facets and mark all the used materials
        for( i=0; i<m_Facet.size<int>(); i++ )
        {
            const facet& Facet = m_Facet[i];
             
            if( Facet.m_iMaterialInstance < 0 ||
                Facet.m_iMaterialInstance > m_MaterialInstance.size<int>() )
                throw(std::runtime_error( std::format("Found a face from mesh [{}] which was using an unknow material FaceID={} MaterialID ={}", 
                    m_Mesh[ Facet.m_iMesh ].m_Name.c_str(),
                    i, Facet.m_iMaterialInstance )));

            Used[ Facet.m_iMaterialInstance ].m_Used = true;
        }

        // Collapse all the material in order
        std::int32_t     nMaterials      = 0;
        bool   bAnyCollapse    = false;
        for( i=0; i<m_MaterialInstance.size<int>(); i++ )
        {
            if( Used[i].m_Used == false )
                continue;

            if ( i == nMaterials )
            {
                Used[nMaterials].m_ReIndex  = i;
            }
            else
            {
                bAnyCollapse = true;
                m_MaterialInstance[ nMaterials ] = m_MaterialInstance[ i ];
                Used[i].m_ReIndex                = nMaterials;
            }

            nMaterials++;
        }

        // Update the material indices for the facets
        if ( bAnyCollapse )
        {
            for( i=0; i<m_Facet.size<int>(); i++ )
            {
                if( Used[ m_Facet[i].m_iMaterialInstance ].m_ReIndex < 0 ||
                    Used[ m_Facet[i].m_iMaterialInstance ].m_ReIndex >= nMaterials )
                    throw(std::runtime_error( "Error while cleaning the materials in the rawgeom2" ));

                m_Facet[i].m_iMaterialInstance = Used[ m_Facet[i].m_iMaterialInstance ].m_ReIndex;
            }

            // Set the new material count
            TotalMaterialsRemoved += m_MaterialInstance.size<int>() - nMaterials;
            if( auto Err = m_MaterialInstance.resize(nMaterials); Err ) throw(std::runtime_error(Err.getCode().m_pString));
        }

        // Sort material parameters
        for ( material_instance& Material : m_MaterialInstance )
        {
            std::sort( Material.m_Params.begin(), Material.m_Params.end() );
        }
    }

    RMESH_SANITY

    //
    // Remove unwanted meshes
    //
    {
        std::int32_t i;
        xcore::unique_span<std::int32_t> SMesh;
        
        if( auto Err = SMesh.New(m_Mesh.size<int>()); Err ) throw(std::runtime_error(Err.getCode().m_pString));
        std::memset( SMesh.data(), 0, SMesh.size_bytes() );
        
        for( i=0; i<m_Facet.size<int>(); i++ )
        {
            SMesh[ m_Facet[i].m_iMesh ] = 1;
        }

        std::int32_t nSubs = 0;
        for( i=0; i<m_Mesh.size<int>(); i++ )
        {
            if( SMesh[ i ] == 1 )
            {
                SMesh[i] = nSubs;
                m_Mesh[nSubs++] = m_Mesh[i];
            }
            else
            {
                SMesh[i] = -1;
            }
        }

        // Set the new count
        if( m_Mesh.size<int>() != nSubs )
            if( auto Err = m_Mesh.resize( nSubs ); Err ) throw(std::runtime_error(Err.getCode().m_pString));
        
        for( i=0; i<m_Facet.size<int>(); i++ )
        {
            xassert( SMesh[ m_Facet[i].m_iMesh ] >= 0 );
            m_Facet[i].m_iMesh = SMesh[ m_Facet[i].m_iMesh ];
        }
    }

    //
    // Sort the meshes so that they are in alphabetical order
    //    
    {
        std::int32_t i, j;

        struct mesh_info
        {
            mesh    m_Mesh;
            std::int32_t         m_iOriginal;
        };

        xcore::unique_span<mesh_info> Mesh;
        xcore::unique_span<std::int32_t>       Remap;
        
        if( auto Err = Mesh.New( m_Mesh.size<int>() ); Err ) throw(std::runtime_error(Err.getCode().m_pString));
            if (auto Err = Remap.New( m_Mesh.size<int>() ); Err ) throw(std::runtime_error(Err.getCode().m_pString));
        
        for( i=0; i<m_Mesh.size<int>(); i++ )
        {
            Mesh[i].m_Mesh       = m_Mesh[i];
            Mesh[i].m_iOriginal  = i;
        }

        // sort the meshes
        bool bSorted = false;
        for ( j = 0; j < m_Mesh.size<int>() && !bSorted; j++ )
        {
            bSorted = true;
            for ( i = 0; i < m_Mesh.size<int>()-1-j; i++ )
            {
                if( xcore::string::Compare( Mesh[i].m_Mesh.m_Name, Mesh[i+1].m_Mesh.m_Name ) > 0 )
                {
                    bSorted        = false;
                    mesh_info Temp = Mesh[i+1];
                    Mesh[i+1]      = Mesh[i];
                    Mesh[i]        = Temp;
                }
            }
        }
        
        // sanity check to make sure we know how to sort
        for ( i = 0; i < m_Mesh.size<int>(); i++ )
        {
            if ( i < m_Mesh.size<int>()-1 )
            {
                xassert( xcore::string::Compare( Mesh[i].m_Mesh.m_Name, Mesh[i+1].m_Mesh.m_Name ) <= 0 );
            }
        }

        // copy over the original Meshes
        for( i=0; i<m_Mesh.size<int>(); i++ )
        {
            m_Mesh[i] = Mesh[i].m_Mesh;
        }

        // build a remap table for the faces
        for( i=0; i<m_Mesh.size<int>(); i++ )
        {
            for ( j=0; j<m_Mesh.size<int>(); j++ )
            {
                if ( Mesh[j].m_iOriginal == i )
                {
                    Remap[i] = j;
                    break;
                }
            }
        }

        // remap the faces
        for( i=0; i<m_Facet.size<int>(); i++ )
        {
            m_Facet[i].m_iMesh = Remap[ m_Facet[i].m_iMesh ];
        }
    }

    //
    // Stats
    //
    printf( "INFO: Total Facets Removed: %d\n",    TotalFacetsRemoved );
    printf( "INFO: Total Verts  Removed: %d\n",    TotalVerticesRemoved );
    printf( "INFO: Total Materials Removed: %d\n", TotalMaterialsRemoved );
}

//--------------------------------------------------------------------------
/*
void CalculateTangentArray( 
    const std::int32_t           vertexCount, 
    const xcore::vector3*    vertex, 
    const xcore::vector3*    normal,
    const xcore::vector2*     texcoord, 
    const std::int32_t           triangleCount, 
    const std::int32_t[3]*       triangle, 
    Vector4D*           tangent )
{
    Vector3D *tan1 = new Vector3D[ vertexCount * 2 ];
    Vector3D *tan2 = tan1 + vertexCount;
    ZeroMemory( tan1, vertexCount * sizeof(Vector3D)* 2 );

    for ( long a = 0; a < triangleCount; a++ )
    {
        long i1 = triangle->index[ 0 ];
        long i2 = triangle->index[ 1 ];
        long i3 = triangle->index[ 2 ];

        const Point3D& v1 = vertex[ i1 ];
        const Point3D& v2 = vertex[ i2 ];
        const Point3D& v3 = vertex[ i3 ];

        const Point2D& w1 = texcoord[ i1 ];
        const Point2D& w2 = texcoord[ i2 ];
        const Point2D& w3 = texcoord[ i3 ];

        float x1 = v2.x - v1.x;
        float x2 = v3.x - v1.x;
        float y1 = v2.y - v1.y;
        float y2 = v3.y - v1.y;
        float z1 = v2.z - v1.z;
        float z2 = v3.z - v1.z;

        float s1 = w2.x - w1.x;
        float s2 = w3.x - w1.x;
        float t1 = w2.y - w1.y;
        float t2 = w3.y - w1.y;

        float r = 1.0F / ( s1 * t2 - s2 * t1 );
        Vector3D sdir( ( t2 * x1 - t1 * x2 ) * r, ( t2 * y1 - t1 * y2 ) * r,
            ( t2 * z1 - t1 * z2 ) * r );
        Vector3D tdir( ( s1 * x2 - s2 * x1 ) * r, ( s1 * y2 - s2 * y1 ) * r,
            ( s1 * z2 - s2 * z1 ) * r );

        tan1[ i1 ] += sdir;
        tan1[ i2 ] += sdir;
        tan1[ i3 ] += sdir;

        tan2[ i1 ] += tdir;
        tan2[ i2 ] += tdir;
        tan2[ i3 ] += tdir;

        triangle++;
    }

    for ( long a = 0; a < vertexCount; a++ )
    {
        const Vector3D& n = normal[ a ];
        const Vector3D& t = tan1[ a ];

        // Gram-Schmidt orthogonalize
        tangent[ a ] = ( t - n * Dot( n, t ) ).Normalize( );

        // Calculate handedness
        tangent[ a ].w = ( Dot( Cross( n, t ), tan2[ a ] ) < 0.0F ) ? -1.0F : 1.0F;
    }

    delete[ ] tan1;
}
*/

//--------------------------------------------------------------------------

void geom::SanityCheck( void ) const
{
    //
    // Check that we have a valid number of materials
    //
    if( m_MaterialInstance.size<int>() < 0 )
        throw(std::runtime_error( "The geom it saying that it has a negative number of materials!"));

    if( m_MaterialInstance.size<int>() > 1000 )
        throw(std::runtime_error( "The rawgeom2 has more than 1000 materials right now that is a sign of a problem" ));
    
    if( m_Vertex.size<int>() < 0 )
        throw(std::runtime_error( "THe rawgeom2 has a negative number of vertices!" ));

    if( m_Vertex.size<int>() > 100000000 )
        throw(std::runtime_error( "The rawgeom2 seems to have more that 100 million vertices that is consider bad" ));

    if( m_Facet.size<int>() < 0 )
        throw(std::runtime_error( "We have a negative number of facets" ));

    if( m_Facet.size<int>() > 100000000 )
        throw(std::runtime_error( "THe rawgeom2 has more thatn 100 million facets that is consider worng" ));

    if( m_Bone.size<int>() < 0 )
        throw (std::runtime_error("We have a negative count for bones" ));
    
    if( m_Bone.size<int>() > 100000 )
        throw (std::runtime_error("We have more than 100,000 bones this may be a problem" ));

    if( m_MaterialInstance.size<int>() < 0 )
        throw(std::runtime_error( "Found a negative number of textures" ));

    if( m_Mesh.size<int>() < 0 )
        throw(std::runtime_error( "We have a negative number of meshes this is not good" ));

    if( m_Mesh.size<int>() > 100000 )
        throw(std::runtime_error( "We have more than 100,000 meshes this may be a problem" ));

    //
    // Check the facets.
    //
    {
        std::int32_t i;
        for( i=0; i<m_Facet.size<int>(); i++ )
        {
            const facet& Facet = m_Facet[i];

            if( Facet.m_nVertices < 0 )
                throw(std::runtime_error( std::format( "I found a facet that had a negative number of indices to vertices Face#:{}", i )));

            if( Facet.m_nVertices < 3 )
                throw(std::runtime_error( std::format("I found a facet with less than 3 vertices Face#:{}", i)));

            if( Facet.m_nVertices > 16 )
                throw(std::runtime_error( std::format("I found a facet with more thatn 16 indices to vertices. The max is 16 this means memory overwun Face#:{}", i )));

            if( Facet.m_iMaterialInstance < 0 )
                throw(std::runtime_error(std::format("I found a facet with a negative material. That is not allow. Facet#:{}",i)));

            if( Facet.m_iMaterialInstance >= m_MaterialInstance.size<int>() )
                throw(std::runtime_error(std::format("I found a facet that had an index to a material which is bad. Facet#:{}",i)));

            if( Facet.m_iMesh < 0 )
                throw(std::runtime_error(std::format("I found a facet indexing to a negative offset for the meshID Facet#:{}",i)));

            if( Facet.m_iMesh >= m_Mesh.size<int>() )
                throw(std::runtime_error(std::format("I found a facet indexing to a not exiting mesh Facet#:{}",i)));

            for( std::int32_t j=0; j<Facet.m_nVertices; j++ )
            {
                if( Facet.m_iVertex[j] < 0 )
                    throw(std::runtime_error(std::format("I found a facet with a negative index to vertices. Facet#:{}",i)));

                if( Facet.m_iVertex[j] >= m_Vertex.size<int>() )
                    throw(std::runtime_error(std::format("I found a facet with a index to a non-exiting vertex. Facet#:{}",i)));
            }
        }
    }

    //
    // Check the vertices.
    //
    {
        std::int32_t i,j;
        for( i=0; i<m_Facet.size<int>(); i++ )
        {
            const facet& Facet = m_Facet[i];
            for( j=0; j<Facet.m_nVertices; j++ )
            {
                const vertex& V = m_Vertex[ Facet.m_iVertex[j] ];

                if( xcore::math::isValid( V.m_Position.m_X ) == false ||
                    xcore::math::isValid( V.m_Position.m_Y ) == false ||
                    xcore::math::isValid( V.m_Position.m_Z ) == false )
                    throw(std::runtime_error(std::format("Just got a infinete vertex position: Vertex#:{}",j)));

                if( V.m_nWeights < 0 )
                    throw(std::runtime_error(std::format("We have a negative count of weights for one of the vertices. V#:{}",j)));

                if( V.m_nWeights >= vertex_max_weights_v )
                    throw(std::runtime_error(std::format("Found a vertex with way too many weights. V#:{}",j)));

                if( V.m_nWeights > m_Bone.size<int>() )
                    throw(std::runtime_error(std::format("Found a vertex pointing to a non-exiting bone: V#:{}",j)));

                if( V.m_nNormals < 0 )
                    throw(std::runtime_error(std::format("Found a vertex with a negative number of Normals. V#:{}",j)));

                if( V.m_nNormals >= vertex_max_normals_v )
                    throw(std::runtime_error(std::format("Found a vertex with way too many normals. V3:{}", j)));

                if( V.m_nColors < 0 )
                    throw(std::runtime_error(std::format("I found a vertex with a negative number of colors. V#:{}", j)));

                if( V.m_nColors >= vertex_max_colors_v )
                    throw(std::runtime_error(std::format("I found a vertex with way too many colors. V#:{}", j)));

                if( V.m_nUVs < 0 )
                    throw(std::runtime_error(std::format("I found a vertex with a negative count for UVs. V#:{}",j)));

                if( V.m_nUVs > vertex_max_uv_v )
                    throw(std::runtime_error(std::format("I found a vertex with way too many UVs. V#:{}", j)));

                //if( V.nUVs != m_pMaterial[ Facet.iMaterial ].GetUVChanelCount() )
                //    throw(std::runtime_error( xfs("I found a vertex that didn't have the right number of UVs for the material been used. V#:%d, F#:%d",j,i));

            }
        }
    }
}

//--------------------------------------------------------------------------

void geom::CleanWeights( std::int32_t MaxNumWeights, float MinWeightValue )
{
    std::int32_t i,j,k;

    //
    // Sort weights from largest to smallest
    //
    for( i=0; i<m_Vertex.size<int>(); i++ )
    {
        for( j=0; j<m_Vertex[i].m_nWeights; j++ )
        {
            std::int32_t BestW = j;
            for( k=j+1; k<m_Vertex[i].m_nWeights; k++ )
            {
                if( m_Vertex[i].m_Weight[k].m_Weight > m_Vertex[i].m_Weight[BestW].m_Weight )
                    BestW = k;
            }

            weight TW = m_Vertex[i].m_Weight[j];
            m_Vertex[i].m_Weight[j] = m_Vertex[i].m_Weight[BestW];
            m_Vertex[i].m_Weight[BestW] = TW;

            xassert(  m_Vertex[i].m_Weight[j].m_iBone >= 0 );
            xassert( m_Vertex[i].m_Weight[j].m_iBone < m_Bone.size<int>() );
        }
    }

    //
    // Cull any extra weights
    //
    for( i=0; i<m_Vertex.size<int>(); i++ )
        if( m_Vertex[i].m_nWeights > MaxNumWeights )
        {
            m_Vertex[i].m_nWeights = MaxNumWeights;

            // Normalize weights
            float TotalW=0.0f;
            for( j=0; j<m_Vertex[i].m_nWeights; j++ )
                TotalW += m_Vertex[i].m_Weight[j].m_Weight;

            for( j=0; j<m_Vertex[i].m_nWeights; j++ )
                m_Vertex[i].m_Weight[j].m_Weight /= TotalW;
        }

    //
    // Throw out all weights below MinWeightValue
    //
    for( i=0; i<m_Vertex.size<int>(); i++ )
    {
        if ( m_Vertex[i].m_nWeights == 0 )
        {
            m_Vertex[ i ].m_Weight[ 0 ].m_iBone = 0;
            m_Vertex[ i ].m_Weight[ 0 ].m_Weight = 1;
            m_Vertex[i].m_nWeights = 1;
            continue;
        }

       // Keep weights above MinWeight
        std::int32_t nWeights = 0;
        for( j=0; j<m_Vertex[i].m_nWeights; j++ )
        {
            if( m_Vertex[i].m_Weight[j].m_Weight >= MinWeightValue )
            {
                m_Vertex[i].m_Weight[nWeights] = m_Vertex[i].m_Weight[j];
                nWeights++;
            }
        }
        
        nWeights = std::max(1,nWeights);
        m_Vertex[i].m_nWeights = nWeights;

        // Normalize weights
        float TotalW=0.0f;
        for( j=0; j<m_Vertex[i].m_nWeights; j++ )
            TotalW += m_Vertex[i].m_Weight[j].m_Weight;
        
        for( j=0; j<m_Vertex[i].m_nWeights; j++ )
            m_Vertex[i].m_Weight[j].m_Weight /= TotalW;


        for ( j = 0; j < m_Vertex[ i ].m_nWeights; j++ )
        {
            xassert( m_Vertex[ i ].m_Weight[ j ].m_iBone >= 0 );
            xassert( m_Vertex[ i ].m_Weight[ j ].m_iBone < m_Bone.size<int>() );
        }
    }
}

//--------------------------------------------------------------------------

void geom::CollapseMeshes( const char* pMeshName )
{
    for( auto& Facet : m_Facet )
    {
        Facet.m_iMesh = 0;
    }

    int MaxBones = -100;
    for( auto& Mesh : m_Mesh )
    {
        MaxBones = std::max( MaxBones, Mesh.m_nBones );
    }
    
    if( auto Err = m_Mesh.resize(1); Err ) throw(std::runtime_error(Err.getCode().m_pString));
    xcore::string::Copy( m_Mesh[0].m_Name, pMeshName );
    m_Mesh[0].m_nBones = MaxBones;
}

//--------------------------------------------------------------------------

void geom::ComputeMeshBBox( std::int32_t iMesh, xcore::bbox& BBox )
{
    std::int32_t i,j;

    BBox.setZero();

    for( i=0; i<m_Facet.size<int>(); i++ )
    {
        if( m_Facet[i].m_iMesh == iMesh )
        {
            for( j=0; j<m_Facet[i].m_nVertices; j++ )
                BBox += m_Vertex[ m_Facet[i].m_iVertex[j] ].m_Position;
        }
    }
}

//--------------------------------------------------------------------------

// Computes bone bboxes and the number of bones used by each Mesh
void geom::ComputeBoneInfo( void )
{
    std::int32_t i,j,k ;

    //=====================================================================
    // Compute bone bboxes
    //=====================================================================

    // Clear all bone bboxes
    for (i = 0 ; i < m_Bone.size<int>() ; i++)
        m_Bone[i].m_BBox.setZero() ;

    // Loop through all the verts and add to bone bboxes
    for (i = 0 ; i < m_Vertex.size<int>() ; i++)
    {
        // Lookup vert
        vertex& Vertex = m_Vertex[i] ;

        // Loop through all weights in vertex
        for (std::int32_t j = 0 ; j < Vertex.m_nWeights ; j++)
        {
            // Lookup bone that vert is attached to
            std::int32_t iBone = Vertex.m_Weight[j].m_iBone ;
            xassert(iBone >= 0) ;
            xassert(iBone < m_Bone.size<int>() ) ;

            // Add to bone bbox
            m_Bone[iBone].m_BBox += Vertex.m_Position ;
        }
    }

    // If a bone has no geometry attached, then set bounds to the bone position
    for (i = 0 ; i < m_Bone.size<int>() ; i++)
    {
        // Lookup bone
        bone& Bone = m_Bone[i] ;

        // If bbox is empty, just use the bone position
        if (Bone.m_BBox.m_Min.m_X > Bone.m_BBox.m_Max.m_X )
            Bone.m_BBox += Bone.m_Position ;

        // Inflate slightly do get rid of any degenerate (flat) sides
        Bone.m_BBox.Inflate( xcore::vector3(0.1f, 0.1f, 0.1f) ) ;
    }

    //=====================================================================
    // Compute # of bones used by each sub-mesh
    // Bones are arranged in LOD order, so we can just use the (MaxBoneUsed+1)
    //=====================================================================
    
    // Clear values
    for (i = 0 ; i < m_Mesh.size<int>(); i++)
        m_Mesh[i].m_nBones = 0 ;

    // Loop through all faces
    for (i = 0 ; i < m_Facet.size<int>() ; i++)
    {
        // Lookup face and the mesh it's part of
        facet&      Face = m_Facet[i] ;
        mesh&   Mesh = m_Mesh[Face.m_iMesh] ;

        // Loop through all verts in each face
        for (j = 0 ; j < Face.m_nVertices ; j++)
        {
            // Lookup vert
            vertex& Vert = m_Vertex[Face.m_iVertex[j]] ;

            // Loop through all weights in vert
            for (k = 0 ; k < Vert.m_nWeights ; k++)
            {
                // Update mesh bone count
                Mesh.m_nBones = std::max( Mesh.m_nBones, Vert.m_Weight[k].m_iBone) ;
            }
        }
    }

    // We want the actual number of bones used so fix up
    for (i = 0 ; i < m_Mesh.size<int>() ; i++)
        m_Mesh[i].m_nBones++ ;
}

//--------------------------------------------------------------------------

bool geom::IsolateMesh( std::int32_t iMesh, geom& NewMesh,
                               bool RemoveFromrawgeom /* = false */)
{
    NewMesh.Kill();

    if( iMesh < 0 )
        return false;
    
    if(iMesh >= m_Mesh.size<int>() )
        return false;

    //
    //  Make straight copies of data we don't affect
    //
    //  BONES
    //
    if( m_Bone.size<int>() > 0 )
    {
        if( auto Err = NewMesh.m_Bone.New(m_Bone.size<int>()); Err ) throw(std::runtime_error(Err.getCode().m_pString));
        if( auto Err = NewMesh.m_Bone.copy( m_Bone ); Err ) throw(std::runtime_error(Err.getCode().m_pString));
    }

    //
    //  Meshes
    //
    xassert( m_Mesh.size<int>() > 0 );
    
    if (auto Err = NewMesh.m_Mesh.New(1); Err) throw(std::runtime_error(Err.getCode().m_pString));
    NewMesh.m_Mesh[0] = m_Mesh[iMesh];
    
    
    //
    // Materials:
    //
    if( m_MaterialInstance.size<int>() > 0 )
    {
        if( auto Err = NewMesh.m_MaterialInstance.New( m_MaterialInstance.size<int>() ); Err ) throw(std::runtime_error(Err.getCode().m_pString));
        if( auto Err = NewMesh.m_MaterialInstance.copy( m_MaterialInstance ); Err ) throw(std::runtime_error(Err.getCode().m_pString));
    }

    // Verts and Facets:
    //
    //  Each facet will generate 3 unique verts, which will be consolidated later.
    //  These are done before the material construction, but the facet iMaterial
    //  member will be touched up in the next step.
    //
    std::int32_t     i;
    std::int32_t     nFacets = 0;

    for (i=0;i<m_Facet.size<int>();i++)
    {
        if (m_Facet[i].m_iMesh == iMesh)
            nFacets++;
    }

    if( auto Err = NewMesh.m_Vertex.New( nFacets * 3 ); Err ) throw(std::runtime_error(Err.getCode().m_pString));
    if( auto Err = NewMesh.m_Facet.New( nFacets ) ) throw(std::runtime_error(Err.getCode().m_pString));

    geom::vertex*    pVert     = &NewMesh.m_Vertex[0];
    std::int32_t                 iVert     = 0;
    geom::facet*     pFacet    = &NewMesh.m_Facet[0];
    
    for (i=0;i<m_Facet.size<int>();i++)
    {
        if (m_Facet[i].m_iMesh == iMesh)
        {
            *pFacet = m_Facet[i];

            pFacet->m_iVertex[0] = iVert+0;
            pFacet->m_iVertex[1] = iVert+1;
            pFacet->m_iVertex[2] = iVert+2;
            iVert+=3;       

            pFacet->m_iMesh = 0;
        
            *pVert = m_Vertex[ m_Facet[i].m_iVertex[0] ];
            pVert++;

            *pVert = m_Vertex[ m_Facet[i].m_iVertex[1] ];
            pVert++;

            *pVert = m_Vertex[ m_Facet[i].m_iVertex[2] ];
            pVert++;

            pFacet++;
        }
    }

    //
    // Clear the mesh
    //
    NewMesh.CleanMesh();
    if (RemoveFromrawgeom)
        CleanMesh(iMesh);

    return true;
}

//--------------------------------------------------------------------------

bool geom::IsolateMesh( const char* pMeshName, geom& NewMesh )
{
    std::int32_t  i;

    if (pMeshName == NULL)
        return false;

    for( i=0;i<m_Mesh.size<int>();i++ )
    {
        if( xcore::string::Compare( m_Mesh[i].m_Name, pMeshName ) == 0 )
        {
            return IsolateMesh( i, NewMesh );
        }
    }

    return false;
}

//--------------------------------------------------------------------------

bool geom::isBoneUsed( std::int32_t iBone )
{
    std::int32_t i,j;

    for( i=0; i<m_Vertex.size<int>(); i++ )
    {
        for( j=0; j<m_Vertex[i].m_nWeights; j++ )
        if( m_Vertex[i].m_Weight[j].m_iBone == iBone )
            return true;
    }

    return false;
}

//--------------------------------------------------------------------------

void geom::CollapseNormals( xcore::radian ThresholdAngle )
{
    float     TargetAngle = xcore::math::Cos( ThresholdAngle );

    struct hash
    {
        std::int32_t         m_iNext;
    };

    struct tempv
    {
        xcore::vector3    m_NewNormal;
        std::int32_t         m_Index;          // Inde to the original vertex
        std::int32_t         m_iNext;          // next node in the has
    };

    if( m_Vertex.size<int>() <= 0 )
        throw(std::runtime_error( "geom has no vertices" ));

    std::int32_t         i;
    std::int32_t         HashSize  = std::max( 20, m_Vertex.size<int>()*10 );
    float         MaxX, MinX, Shift;

    // Allocate memory
    xcore::unique_span<hash>       Hash;
    xcore::unique_span<tempv>      TempV;
    
    if( auto Err = Hash.New( HashSize ); Err ) throw(std::runtime_error(Err.getCode().m_pString));
    if( auto Err = TempV.New( m_Vertex.size<int>() ); Err ) throw(std::runtime_error(Err.getCode().m_pString));
    
    // Initialize the hash with terminators
    for( i=0; i<HashSize; i++) 
    {
        Hash[i].m_iNext = -1;
    }

    // Fill the nodes for each of the dimensions
    MaxX = m_Vertex[0].m_Position.m_X;
    MinX = MaxX;
    for( i=0; i<m_Vertex.size<int>(); i++)
    {
        TempV[i].m_Index         =  i;
        TempV[i].m_iNext         = -1;
        TempV[i].m_NewNormal.setup(0,0,0);
       
        MaxX = std::max( MaxX, m_Vertex[i].m_Position.m_X );
        MinX = std::min( MinX, m_Vertex[i].m_Position.m_X );
    }

    // Hash all the vertices into the hash table
    Shift = HashSize/(MaxX-MinX+1);
    for( i=0; i<m_Vertex.size<int>(); i++)
    {
        std::int32_t OffSet = (std::int32_t)(( m_Vertex[i].m_Position.m_X - MinX ) * Shift);

        xassert(OffSet >= 0 );
        xassert(OffSet < HashSize );

        TempV[i].m_iNext  = Hash[ OffSet ].m_iNext;
        Hash[ OffSet ].m_iNext = i;
    }

    // Loop through all hash entries, and begin the collapse process
    for( i=0; i<HashSize; i++ )
    {
        for( std::int32_t k = Hash[i].m_iNext; k != -1; k = TempV[k].m_iNext )
        {
            std::int32_t         j;
            xcore::vector3    SrcN = m_Vertex[ TempV[k].m_Index ].m_BTN[0].m_Normal;
            xcore::vector3    SrcP = m_Vertex[ TempV[k].m_Index ].m_Position;
            xcore::vector3    ResultN = SrcN;
            
            for( j = Hash[i].m_iNext; j != -1; j = TempV[j].m_iNext )
            {                
                if (j==k)
                    continue;
                
                xcore::vector3 D = m_Vertex[ TempV[j].m_Index ].m_Position - SrcP;

                //  If the verts don't share the same position, continue
                if( D.getLength() > 0.001f )
                    continue;

                //
                //  Check the normals to see if the 2nd vert's norm is within the
                //  allowable threshold
                //
                xcore::vector3 N = m_Vertex[ TempV[j].m_Index ].m_BTN[0].m_Normal;

                float      T = SrcN.Dot( N );
                if ( T >= TargetAngle )
                {
                    // Merge in this normal
                    ResultN += N;
                }
            }

            // Search in the hash on the right
            if( (i+1)< HashSize )
            {
                for( j = Hash[i+1].m_iNext; j != -1; j = TempV[j].m_iNext )
                {                
                    xcore::vector3 D = m_Vertex[ TempV[j].m_Index ].m_Position - SrcP;

                    //  If the verts don't share the same position, continue
                    if (D.getLength() > 0.001f)
                        continue;

                    //
                    //  Check the normals to see if the 2nd vert's norm is within the
                    //  allowable threshold
                    //
                    xcore::vector3 N = m_Vertex[ TempV[j].m_Index ].m_BTN[0].m_Normal;

                    float     T = SrcN.Dot( N );
                    if ( T >= TargetAngle )
                    {
                        // Merge in this normal
                        ResultN += N;
                    }
                }
            }
            
            // Renormalize the resultant normal
            ResultN.Normalize();

            TempV[k].m_NewNormal = ResultN;
        }
    }

    for (i=0;i<m_Vertex.size<int>();i++)
    {
        m_Vertex[ TempV[i].m_Index ].m_BTN[0].m_Normal = TempV[i].m_NewNormal;
    }
}

//--------------------------------------------------------------------------

void geom::DeleteBone( const char* pBoneName )
{
    std::int32_t iBone = getBoneIDFromName( pBoneName );
    if(iBone != -1)
        DeleteBone(iBone);
}

//--------------------------------------------------------------------------

void geom::DeleteBone( std::int32_t iBone )
{
    //x_DebugMsg("MESH: Deleting bone: '%s'\n", m_pBone[iBone].Name);
    std::int32_t i,j;
    xassert( m_Bone.size<int>() > 1 );

    //
    // Allocate new bones and frames
    //
    xcore::unique_span<bone> NewBone;
    if( auto Err = NewBone.New( m_Bone.size<int>() - 1 ); Err) throw(std::runtime_error(Err.getCode().m_pString));
    
    //
    // Build new hierarchy
    //
    {
        // Copy over remaining bones
        j=0;
        for( i=0; i<m_Bone.size<int>(); i++ )
        if( i != iBone )
        {
            NewBone[j] = m_Bone[i];
            j++;
        }

        // Patch children of bone
        for( i=0; i<NewBone.size<int>(); i++ )
        if( NewBone[i].m_iParent == iBone )
        {
            NewBone[i].m_iParent = m_Bone[iBone].m_iParent;
        }

        // Patch references to any bone > iBone
        for( i=0; i<NewBone.size<int>(); i++ )
        if( NewBone[i].m_iParent > iBone )
        {
            NewBone[i].m_iParent--;
        }
    }

    // free current allocations
    m_Bone = std::move(NewBone);
}

//--------------------------------------------------------------------------

std::int32_t geom::getBoneIDFromName( const char* pBoneName ) const
{
    std::int32_t i;
    for( i=0; i<m_Bone.size<int>(); i++ )
    if( xcore::string::CompareI( pBoneName, m_Bone[i].m_Name) == 0 )
        return i;
    return -1;
}

//--------------------------------------------------------------------------

void geom::ApplyNewSkeleton( const anim& Skel )
{
    std::int32_t i,j;

    // Transform all verts into local space of current skeleton
    if(/* DISABLES CODE */ (0))
    for(i = 0; i < m_Vertex.size<int>(); i++)
    {
        auto& Vertex = m_Vertex[i];

        std::int32_t         iBone   = Vertex.m_Weight[0].m_iBone;
        xcore::vector3    P       = Vertex.m_Position;
        xcore::matrix4    BM;
        
        BM.identity();
        BM.Scale( m_Bone[iBone].m_Scale );
        BM.Rotate( m_Bone[iBone].m_Rotation );
        BM.Translate( m_Bone[iBone].m_Position );
        BM.InvertSRT();
        
        Vertex.m_Position = BM * P;
    }

    // Remap bone indices
    for(i = 0; i < m_Vertex.size<int>(); i++)
    {
        geom::vertex* pVertex = &m_Vertex[i];
        for(std::int32_t iWeight = 0; iWeight < pVertex->m_nWeights; iWeight++)
        {
            geom::weight* pWeight = &pVertex->m_Weight[iWeight];

            std::int32_t oldBoneId = pWeight->m_iBone;
            std::int32_t newBoneId = -1;
            std::int32_t curBoneId = oldBoneId;
            while( (newBoneId==-1) && (curBoneId!=-1) )
            {
                const char* curBoneName = m_Bone[curBoneId].m_Name;

                // Look for matching bone in Skel
                for( j=0; j<Skel.m_Bone.size<int>(); j++ )
                    if( xcore::string::CompareI( curBoneName, Skel.m_Bone[j].m_Name ) == 0 )
                        break;

                if( j != Skel.m_Bone.size<int>() )
                {
                    newBoneId = j;
                    break;
                }

                // Move up hierarchy to parent
                curBoneId = m_Bone[curBoneId].m_iParent;
            }

            if ( newBoneId == -1 )
            {
                newBoneId = 0;
                printf( "WARNING: Unable to remap Bone Vertex[%d] to new bone", i );
            }

            //xassert( m_pBone[pWeight->iBone].Position.Difference( Skel.GetBone(newBoneId).BindTranslation ) < 0.0001f );
            //xassert( m_pBone[pWeight->iBone].Rotation.Difference( Skel.GetBone(newBoneId).BindRotation ) < 0.0001f );
            //x_DebugMsg("For old bone of %d, found new bone %d\n", pWeight->iBone, newBoneId);
            pWeight->m_iBone = newBoneId;
            //x_DebugMsg("%s -> %s\n",m_pBone[oldBoneId].Name,Skel.m_pBone[newBoneId].Name);
        }
    }

    //
    // Copy new bone information in
    //
    if( auto Err = m_Bone.New( Skel.m_Bone.size<int>() ); Err ) throw(std::runtime_error(Err.getCode().m_pString));
    for( std::int32_t count = 0; count < m_Bone.size<int>(); count++ )
    {
        m_Bone[count].m_iParent     =   Skel.m_Bone[count].m_iParent;
        m_Bone[count].m_nChildren   =   Skel.m_Bone[count].m_nChildren;
        
        xcore::string::Copy( m_Bone[count].m_Name, Skel.m_Bone[count].m_Name );

        m_Bone[count].m_Position    =   Skel.m_Bone[count].m_BindTranslation;
        m_Bone[count].m_Rotation    =   Skel.m_Bone[count].m_BindRotation;
        m_Bone[count].m_Scale       =   Skel.m_Bone[count].m_BindScale;
    }

    // Transform all verts into model space of new skeleton
    if ( /* DISABLES CODE */ (0) )
    for(i = 0; i < m_Vertex.size<int>(); i++)
    {
        std::int32_t         iBone   = m_Vertex[i].m_Weight[0].m_iBone;
        xcore::vector3    P       = m_Vertex[i].m_Position;
        xcore::matrix4    BM;
        BM.identity();
        BM.Scale        ( m_Bone[iBone].m_Scale );
        BM.Rotate       ( m_Bone[iBone].m_Rotation );
        BM.Translate    ( m_Bone[iBone].m_Position );
        m_Vertex[i].m_Position = BM * P;
    }

    // Compute all bone related info
    ComputeBoneInfo() ;
}

//--------------------------------------------------------------------------

void geom::ApplyNewSkeleton( const geom& Skel )
{
    std::int32_t i;

    // Transform all verts into local space of current skeleton
    for(i = 0; i < m_Vertex.size<int>(); i++)
    {
        std::int32_t         iBone   = m_Vertex[i].m_Weight[0].m_iBone;
        xcore::vector3    P       = m_Vertex[i].m_Position;
        xcore::matrix4    BM;
        
        BM.identity();
        BM.Scale    ( m_Bone[iBone].m_Scale );
        BM.Rotate   ( m_Bone[iBone].m_Rotation );
        BM.Translate( m_Bone[iBone].m_Position );
        BM.InvertSRT();
        m_Vertex[i].m_Position = BM * P;
    }

    for( std::int32_t iVertex = 0; iVertex < m_Vertex.size<int>(); iVertex++ )
    {
        vertex* pVertex = &m_Vertex[iVertex];
        for(std::int32_t iWeight = 0; iWeight < pVertex->m_nWeights; iWeight++)
        {
            weight*     pWeight     = &pVertex->m_Weight[iWeight];
            std::int32_t         oldBoneId   = pWeight->m_iBone;
            const char* oldBoneName = m_Bone[oldBoneId].m_Name;
            std::int32_t         newBoneId   = -1;
            std::int32_t         curBoneId   = oldBoneId;
            const char* curBoneName = oldBoneName;
            
            while(newBoneId == -1)
            {
                //x_DebugMsg("Looking in new skeleton for name '%s'\n", curBoneName);
                newBoneId = Skel.getBoneIDFromName(curBoneName);
                curBoneId = m_Bone[curBoneId].m_iParent;
                xassert((newBoneId !=-1) || (curBoneId != -1));
                curBoneName = m_Bone[curBoneId].m_Name;
            }
            //x_DebugMsg("For old bone of %d, found new bone %d\n", pWeight->iBone, newBoneId);
            pWeight->m_iBone = newBoneId;
        }
    }

    // Copy new bone information in
    if( auto Err = m_Bone.New( Skel.m_Bone.size<int>() ); Err ) throw(std::runtime_error(Err.getCode().m_pString));

    for(std::int32_t count = 0; count < m_Bone.size<int>(); count++)
    {
        const bone& Bone = Skel.m_Bone[count];
        m_Bone[count] = Bone;
    }

    // Transform all verts into local space of current skeleton
    for(i = 0; i < m_Vertex.size<int>(); i++)
    {
        std::int32_t         iBone   = m_Vertex[i].m_Weight[0].m_iBone;
        xcore::vector3    P       = m_Vertex[i].m_Position;
        xcore::matrix4    BM;
        
        BM.identity();
        BM.Scale    ( m_Bone[iBone].m_Scale );
        BM.Rotate   ( m_Bone[iBone].m_Rotation );
        BM.Translate( m_Bone[iBone].m_Position );
        
        m_Vertex[i].m_Position = BM * P;
    }
}

} // namespace xraw3d