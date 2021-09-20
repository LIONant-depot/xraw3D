namespace xraw3d
{
    class anim
    {
    public:
        
        static constexpr auto num_event_strings_v = 5;
        static constexpr auto num_event_ints_v    = 5;
        static constexpr auto num_event_floats_v  = 8;
        static constexpr auto num_event_bools_v   = 8;
        static constexpr auto num_event_colors_v  = 4;
        
        struct bone
        {
            string                  m_Name;
            std::int32_t            m_iParent;
            std::int32_t            m_nChildren;

            xcore::vector3          m_BindTranslation;
            xcore::quaternion       m_BindRotation;
            xcore::vector3          m_BindScale;

            bool                    m_bScaleKeys;
            bool                    m_bRotationKeys;
            bool                    m_bTranslationKeys;
            bool                    m_bIsMasked;

            xcore::matrix4          m_BindMatrix;
            xcore::matrix4          m_BindMatrixInv;
        };

        using key_frame = xcore::math::transform3;

        struct event
        {
            string                  m_Name;
            string                  m_ParentName;
            std::int32_t            m_Type;
            float                   m_Radius;
            std::int32_t            m_Frame0;
            std::int32_t            m_Frame1;
            xcore::vector3          m_Position;
        };

        struct super_event
        {
            string                  m_Name;

            std::int32_t            m_Type;
            std::int32_t            m_StartFrame;
            std::int32_t            m_EndFrame;
            xcore::vector3          m_Position;
            xcore::quaternion       m_Rotation;
            float                   m_Radius;

            bool                    m_ShowAxis;
            bool                    m_ShowSphere;
            bool                    m_ShowBox;

            float                   m_AxisSize;

            float                   m_Width;
            float                   m_Length;
            float                   m_Height;

            std::array<string     ,  num_event_strings_v>   m_Strings;
            std::array<std::int32_t, num_event_ints_v>      m_Ints;
            std::array<float,        num_event_floats_v>    m_Floats;
            std::array<bool,         num_event_bools_v>     m_Bools;
            std::array<xcore::icolor,num_event_colors_v>    m_Colors;
        };
        
        struct prop_frame
        {
            xcore::vector3          m_Scale;
            xcore::quaternion       m_Rotation;
            xcore::vector3          m_Translation;
            bool                    m_bVisible;
        };

        struct prop
        {
            xcore::string::ref<char>             m_Name;
            std::int32_t            m_iParentBone;
            string                  m_Type;
        };

    public:
        
        void                    Serialize               ( bool                          isRead
                                                        , const char*                   pFileName
                                                        , xcore::textfile::file_type    Type      = xcore::textfile::file_type::BINARY
                                                        );
        void                    Save                    ( const char*       pFileName 
                                                        ) const;
        void                    CleanUp                 ( void 
                                                        ) ;
        bool                    AreBonesFromSameBranch  ( const std::int32_t iBoneA
                                                        , const std::int32_t iBoneB 
                                                        ) const ;
        void                    PutBonesInLODOrder      ( void 
                                                        );
        void                    ComputeBonesL2W         ( xcore::matrix4*   pMatrix
                                                        , float             Frame 
                                                        ) const ;
        void                    ComputeBonesL2W         ( xcore::matrix4*   pMatrix
                                                        , std::int32_t      iFrame
                                                        , bool              bRemoveHorizMotion
                                                        , bool              bRemoveVertMotion
                                                        , bool              bRemoveYawMotion 
                                                        ) const  ;
        void                    ComputeBoneL2W          ( std::int32_t      iBone
                                                        , xcore::matrix4&   Matrix
                                                        , float             Frame 
                                                        ) const ;
        void                    ComputeRawBoneL2W       ( std::int32_t      iBone
                                                        , xcore::matrix4&   Matrix
                                                        , std::int32_t      iFrame 
                                                        ) const ;
        std::int32_t            GetBoneIDFromName       ( const char*       pBoneName 
                                                        ) const;
        void                    ComputeBoneKeys         ( xcore::quaternion* pQ
                                                        , xcore::vector3*    pS
                                                        , xcore::vector3*    pT
                                                        , float              Frame 
                                                        ) const ;
        void                    BakeBindingIntoFrames   ( bool              BakeScale
                                                        , bool              BakeRotation
                                                        , bool              BakeTranslation 
                                                        ) ;
        void                    RemoveFramesFromRage    ( std::int32_t      StartingValidRange
                                                        , std::int32_t      EndingValidRange 
                                                        ) ;
        void                    DeleteBone              ( std::int32_t      iBone 
                                                        ) ;
        void                    DeleteBone              ( const char*       pBoneName 
                                                        ) ;
        void                    DeleteDummyBones        ( void              // Deletes all bones with "dummy" in the name
                                                        ) ;
        bool                    ApplyNewSkeleton        ( const anim&       BindAnim 
                                                        ) ;
        bool                    HasSameSkeleton         ( const anim&       Anim 
                                                        ) const ;
        void                    setNewRoot              ( std::int32_t      Index 
                                                        ) ;
        void                    SanityCheck             ( void 
                                                        ) const ;
        bool                    isMaskedAnim            ( void 
                                                        ) const ;
        void                    CopyFrames              ( xcore::unique_span<key_frame>&  KeyFrame
                                                        , std::int32_t      iStart
                                                        , std::int32_t      nFrames 
                                                        ) const ;
        void                    InsertFrames            ( std::int32_t      iDestFrame
                                                        , xcore::unique_span<key_frame>& KeyFrame 
                                                        ) ;
        void                    RencenterAnim           ( bool              TX
                                                        , bool              TY
                                                        , bool              TZ
                                                        , bool              Pitch
                                                        , bool              Yaw
                                                        , bool              Roll 
                                                        ) ;
        void                    CleanLoopingAnim        ( void 
                                                        ) ;
        anim&                operator =                 ( const anim&       Src 
                                                        );
    public:

        std::int32_t                        m_nFrames               {0};
        string                              m_Name                  {};
        std::int32_t                        m_FPS                   {60};

        xcore::unique_span<bone>            m_Bone                  {};
        xcore::unique_span<key_frame>       m_KeyFrame              {};                  // Bones in the Columns, key frames on the Rows
        xcore::unique_span<event>           m_Event                 {};
        xcore::unique_span<super_event>     m_SuperEvent            {};
        xcore::unique_span<prop>            m_Prop                  {};
        xcore::unique_span<prop_frame>      m_PropFrame             {};
    };

} // namespace xraw3d
