namespace xraw3d
{
    class geom
    {
    public:

        static constexpr auto vertex_max_uv_v               = 8;
        static constexpr auto vertex_max_normals_v          = 3;
        static constexpr auto vertex_max_colors_v           = 4;
        static constexpr auto vertex_max_weights_v          = 16;
        static constexpr auto facet_max_vertices_v          = 8;
        static constexpr auto material_max_maps_v           = 16;
        static constexpr auto material_max_constants_v      = 8;
        static constexpr auto param_pkg_max_items_v         = 4;

        struct bone
        {
            string                                          m_Name;
            std::int32_t                                    m_nChildren;
            std::int32_t                                    m_iParent;
            xcore::vector3                                  m_Scale;
            xcore::quaternion                               m_Rotation;
            xcore::vector3                                  m_Position;
            xcore::bbox                                     m_BBox;
        };

        struct mesh
        {
            string                                          m_Name;
            std::int32_t                                    m_nBones;

            const mesh& operator = ( const mesh&A )
            {
                m_Name   = A.m_Name;
                m_nBones = A.m_nBones;
                return *this;
            }
        };

        struct weight
        {
            std::int32_t                                    m_iBone;
            float                                           m_Weight;
        };

        struct btn
        {
            xcore::vector3                                  m_Binormal;
            xcore::vector3                                  m_Tangent;
            xcore::vector3                                  m_Normal;
        };

        struct vertex
        {
            xcore::vector3                                  m_Position;

            std::int32_t                                    m_iFrame        = 0;
            std::int32_t                                    m_nWeights      = 0;
            std::int32_t                                    m_nNormals      = 0;
            std::int32_t                                    m_nTangents     = 0;
            std::int32_t                                    m_nBinormals    = 0;
            std::int32_t                                    m_nUVs          = 0;
            std::int32_t                                    m_nColors       = 0;

            std::array<xcore::vector2,vertex_max_uv_v>      m_UV;
            std::array<xcore::icolor, vertex_max_colors_v>  m_Color;
            std::array<weight,        vertex_max_weights_v> m_Weight;
            std::array<btn,           vertex_max_normals_v> m_BTN;
        };

        struct facet
        {
            std::int32_t                                    m_iMesh;
            std::int32_t                                    m_nVertices;
            std::array<std::int32_t,facet_max_vertices_v>   m_iVertex;
            std::int32_t                                    m_iMaterialInstance;
            xcore::plane                                    m_Plane;
        };

        // Material refers to a material instance not a material-shader/type
        // The material insance has a reference of the material shader such multiple instances
        // could in refer to the same material shader. Ideally a mesh should just point to
        // an actual material instance so that hopefully many meshes use a single instance.
        // this will the best case in terms of performance. Because all the meshes that can be
        // render with the game material instance dont need any state changes except for vertex/index buffers.
        // However most games dont really use this concept very much and most meshes have their own material instances,
        // with custom textures and tweaked parameters.
        struct material_instance
        {
            enum class params_type : std::uint8_t
            { INVALID
            , BOOL
            , F1
            , F2
            , F3
            , F4
            , TEXTURE
            , ENUM_COUNT
            };

            static auto& getTypeString( params_type e )
            {
                static constexpr auto strings_v = std::array
                { xcore::string::constant{ "NULL"    }
                , xcore::string::constant{ "BOOL"    }
                , xcore::string::constant{ "F1"      }
                , xcore::string::constant{ "F2"      }
                , xcore::string::constant{ "F3"      }
                , xcore::string::constant{ "F4"      }
                , xcore::string::constant{ "TEXTURE" }
                };
                return strings_v[ int(e) ];
            }

            struct params
            {
                bool operator < ( const params& B ) const
                {
                    std::int32_t Answer = std::strcmp( m_Name.data(), B.m_Name.data() );
                    return Answer <= 0;
                }

                params_type                             m_Type;
                std::array<char,256>                    m_Name;
                std::array<char,256>                    m_Value;
            };

            material_instance& operator = ( const material_instance& Material )
            {
                if ( this != &Material )
                {
                    m_Name           = Material.m_Name;
                    m_MaterialShader = Material.m_MaterialShader;
                    m_Technique      = Material.m_Technique;
                    m_Params         = Material.m_Params;
                }
                return *this;
            }

            string                                      m_Name;
            string                                      m_MaterialShader;
            string                                      m_Technique;
            xcore::vector<params>                       m_Params;
        };

    public:

        void                    Serialize                   ( bool                          isRead
                                                            , const char*                   pFileName
                                                            , xcore::textfile::file_type    Type      = xcore::textfile::file_type::BINARY
                                                            );
        void                    Kill                        ( void 
                                                            );
        void                    SanityCheck                 ( void 
                                                            ) const;
        void                    CleanMesh                   ( std::int32_t                  iSubMesh = -1 
                                                            );  
        void                    CleanWeights                ( std::int32_t                  MaxNumWeights
                                                            , float                         MinWeightValue 
                                                            );
        void                    ForceAddColorIfNone         ( void 
                                                            );
        void                    CollapseMeshes              ( const char*                   pMeshName 
                                                            );
        void                    CollapseNormals             ( xcore::radian                 ThresholdAngle = 20_xdeg 
                                                            );
        xcore::bbox             getBBox                     ( void 
                                                            );
        void                    ComputeMeshBBox             ( std::int32_t                  iMesh
                                                            , xcore::bbox&                  BBox 
                                                            );
        void                    ComputeBoneInfo             ( void 
                                                            );
        bool                    IsolateMesh                 ( std::int32_t                  iSubmesh
                                                            , geom&                         NewMesh
                                                            , bool                          RemoveFromRawMesh = false 
                                                            );
        bool                    IsolateMesh                 ( const char* pMeshName
                                                            , geom&                         NewMesh 
                                                            );
        bool                    isBoneUsed                  ( std::int32_t                  iBone 
                                                            );
        std::int32_t            getBoneIDFromName           ( const char*                   pBoneName 
                                                            ) const;
        void                    DeleteBone                  ( std::int32_t                  iBone 
                                                            );
        void                    DeleteBone                  ( const char*                   pBoneName 
                                                            );        
        void                    ApplyNewSkeleton            ( const anim&                   RawAnim 
                                                            );
        void                    ApplyNewSkeleton            ( const geom&                   Skel 
                                                            );
        void                    SortFacetsByMaterial        ( void 
                                                            );
        void                    SortFacetsByMeshMaterialBone( void 
                                                            );
        static bool             TempVCompare                ( const geom::vertex&           A
                                                            , const geom::vertex&           B 
                                                            );
        static bool             CompareFaces                ( const geom::facet&            A
                                                            , const geom::facet&            B 
                                                            );
    public:

        xcore::unique_span<bone>                  m_Bone;
        xcore::unique_span<vertex>                m_Vertex;
        xcore::unique_span<facet>                 m_Facet;
        xcore::unique_span<material_instance>     m_MaterialInstance;
        xcore::unique_span<mesh>                  m_Mesh;
    };

} // xraw3d