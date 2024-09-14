namespace xraw3d {

//--------------------------------------------------------------------------

bool anim::AreBonesFromSameBranch( std::int32_t iBoneA, std::int32_t iBoneB ) const
{
    std::int32_t iParent;

    // Check for finding B in A
    iParent =  m_Bone[ iBoneA ].m_iParent;// GetBoneIDFromName( m_Bone[ iBoneA ].m_ParentName );
    while( iParent != -1 )
    {
        if( iParent == iBoneB )
            return true ;

        iParent = m_Bone[iParent].m_iParent;// GetBoneIDFromName( m_Bone[iParent].m_ParentName );
    }

    // Check for finding A in B
    iParent = m_Bone[iBoneB].m_iParent;// GetBoneIDFromName( m_Bone[iBoneB].m_ParentName );
    while(iParent != -1)
    {
        if (iParent == iBoneA)
            return true ;

        iParent = m_Bone[iParent].m_iParent; // GetBoneIDFromName( m_Bone[iParent].m_ParentName );
    }

    return false ;
}

//--------------------------------------------------------------------------

anim& anim::operator =( const anim& Src )  
{ 
    m_nFrames   = Src.m_nFrames;
    m_FPS       = Src.m_FPS;
    m_Name      = Src.m_Name;

    xcore::err Error;
       ( Error = m_Bone.copy         ( Src.m_Bone )         )
    || ( Error = m_KeyFrame.copy     ( Src.m_KeyFrame)      )
    || ( Error = m_Event.copy        ( Src.m_Event )        )
    || ( Error = m_SuperEvent.copy   ( Src.m_SuperEvent )   )
    || ( Error = m_Prop.copy         ( Src.m_Prop )         )
    || ( Error = m_PropFrame.copy    ( Src.m_PropFrame )    )
    ;
    if( Error ) throw(std::runtime_error(Error.getCode().m_pString));

    return *this;
}

//--------------------------------------------------------------------------

struct temp_bone : public anim::bone
{
    std::int32_t    m_iBone;
    std::int32_t    m_iBoneRemap;
    std::int32_t    m_LODGroup;
    std::int32_t    m_Depth;
};

//--------------------------------------------------------------------------

// Gets bones in optimzal order ready for skeleton LOD
void anim::PutBonesInLODOrder( void )
{
    // Create temp bones
    xcore::unique_span<temp_bone> TempBone;
    if( TempBone.New( m_Bone.size() ) )
        throw("Out of memory while new temp bones");

    //
    // Initialize structures
    //
    for ( temp_bone& TBone : TempBone )
    {
        const std::int32_t  Index   = std::int32_t(&TBone - &TempBone[0]);
        const bone&         Bone    = m_Bone[ Index ];

        *(bone*)&TBone      = Bone;
        TBone.m_iBoneRemap  = Index;
        TBone.m_iBone       = Index;

        //
        // Set the LOD group of the bone
        //
        auto iLodGroupStart = xcore::string::FindStr( Bone.m_Name, "LOD[" );
        if ( iLodGroupStart != -1 )
        {
            auto iLodGroupEnd = xcore::string::FindStr( &Bone.m_Name[iLodGroupStart+1], "]" );
            if ( iLodGroupEnd == -1 )
                throw( std::runtime_error( std::format( "ERROR: We found a bone[{}] with an LOD group but with a missing ']' ", Bone.m_Name.c_str() )));

            std::array<char,32> Buffer;
            std::int32_t  Length = std::int32_t(iLodGroupEnd - iLodGroupStart);
            xassert( Length < sizeof(Buffer) );

            strncpy_s( Buffer.data(), Buffer.size(), &Bone.m_Name[ iLodGroupStart + 4 ], Length );

            TBone.m_LODGroup = std::atoi( Buffer.data() );
            xassert( TBone.m_LODGroup >= 0 );
            xassert( TBone.m_LODGroup <= 1000 );
        }
        else
        {
            if ( TBone.m_iParent == -1 ) 
                TBone.m_LODGroup = -2;
            else
                TBone.m_LODGroup = -1;
        }

        //
        // Setup depths
        //
        TBone.m_Depth = 0;
        for( std::int32_t i = TBone.m_iParent; i != -1; i = m_Bone[i].m_iParent )
        {
            xassert( TBone.m_iParent != -1 );

            // Cascade the LOD group down if the user did not specify any new groups
            if ( TBone.m_LODGroup == -1 && TempBone[ TBone.m_iParent ].m_LODGroup >= 0 )
            {
                TBone.m_LODGroup = TempBone[TBone.m_iParent].m_LODGroup;
            }
            else if ( TempBone[ TBone.m_iParent ].m_LODGroup > TBone.m_LODGroup )
            {
                printf( "WARNING: You have specify a bone[%s] LOD group that is lower than the parent[%s], we will assign the same group as the parent.",
                        TBone.m_Name.c_str(),
                        TempBone[TBone.m_iParent].m_Name.c_str() );

                TBone.m_LODGroup = TempBone[TBone.m_iParent].m_LODGroup;
            }

            // Increment the depth
            ++TBone.m_Depth;
        }
    }

    //
    // Short bones properly
    //
    std::qsort( TempBone.data(), TempBone.size(), sizeof( temp_bone ), []( const void* pA, const void* pB ) -> std::int32_t
    {
        const temp_bone& A = *(const temp_bone*)pA;
        const temp_bone& B = *(const temp_bone*)pB;

        if ( A.m_LODGroup < B.m_LODGroup ) return -1;
        if ( A.m_LODGroup > B.m_LODGroup ) return  1;

        if ( A.m_iParent < B.m_iParent ) return -1;
        if ( A.m_iParent > B.m_iParent ) return  1;

        if ( A.m_iBone < B.m_iBone ) return -1;
        return ( A.m_iBone > B.m_iBone );
    } );

    // Setup remap bone indices
    for( std::int32_t i = 0 ; i < TempBone.size(); i++ )
        TempBone[ TempBone[i].m_iBone ].m_iBoneRemap = i ;

    // Remap parent indices
    for( std::int32_t i = 0 ; i < TempBone.size(); i++ )
    {
        std::int32_t iParent = TempBone[i].m_iParent ;
        if (iParent != -1)
            TempBone[i].m_iParent = TempBone[iParent].m_iBoneRemap ;
    }

    // Copy out the temp bones into the real bones
    for( std::int32_t i = 0 ; i < TempBone.size(); i++ )
    {
        m_Bone[i] = TempBone[i];
    }

    // Validate
    for (std::int32_t i = 0 ; i < m_Bone.size() ; i++)
    {
        const auto& Bone = m_Bone[i];
        
        // Parent should always be above child!
        if( Bone.m_iParent >= i )
            throw( std::runtime_error("ERROR: Bone LOD Sort has failed!!! Make sure that LOD groups are setup correctly\n"));
    }

    // Remap all the keys
    xcore::unique_span<key_frame> NewKeys;
    if( NewKeys.New( m_KeyFrame.size() ) )
        throw( std::runtime_error("out of memory while new key"));
    const std::int32_t nBones = m_Bone.size<std::int32_t>();
    for ( std::int32_t iKey = 0; iKey < m_KeyFrame.size(); iKey++ )
    {
        const std::int32_t iFrame    = iKey / nBones;
        const std::int32_t iBone     = iKey % m_Bone.size();
        const std::int32_t iOld      = (iFrame * nBones) + TempBone[ iBone ].m_iBone; 
        
        NewKeys[ iKey ] = m_KeyFrame[ iOld ];
    }

    // set the new list
    if( auto Err = m_KeyFrame.copy( NewKeys ); Err ) throw(std::runtime_error(Err.getCode().m_pString));

    // Show hierarchy in debug window
    /*
    x_DebugMsg("\n\n\nLOD optimized bone order:\n") ;
    for( std::int32_t i = 0 ; i < m_Bone.size<int>() ; i++)
    {
        std::int32_t iParent ;

        // Print indent
        iParent = m_Bone[i].m_iParent ;
        while(iParent != -1)
        {
            x_DebugMsg(" ") ;
            iParent = m_Bone[iParent].m_iParent ;
        }

        // Print name
        iParent = m_Bone[i].m_iParent ;
        xassert(iParent < i) ;
        if (iParent != -1)
            x_DebugMsg("%s (Parent=%s)\n", (const char*)m_Bone[i].m_Name, (const char*)m_Bone[iParent].m_Name) ;
        else
            x_DebugMsg("%s\n", (const char*)m_Bone[i].m_Name) ;
    }
    x_DebugMsg("\n\n\n") ;
    */
}

//--------------------------------------------------------------------------

void anim::Serialize
( bool                          isRead
, const char*                   pFileName
, xcore::textfile::file_type    FileType
)
{
    xcore::textfile::stream File;
    xcore::err              Error;

    if( auto Err = File.Open(isRead, pFileName, FileType ); Err )
        throw(std::runtime_error( Err.getCode().m_pString ));

    if( File.Record
        ( Error
        , "AnimInfo"
        , [&]( std::size_t, xcore::err& Err )
        {
               (Err = File.Field( "Name",    m_Name ))
            || (Err = File.Field( "FPS",     m_FPS ))
            || (Err = File.Field( "nFrames", m_nFrames ))
            ;
        })
      ) throw(std::runtime_error(Error.getCode().m_pString));;

    if( File.Record
        ( Error
        , "Skeleton"
        , [&]( std::size_t& C, xcore::err& Err )
        {
            if(isRead) Err = m_Bone.New( C );
            else       C = m_Bone.size();
        }
        , [&](std::size_t I, xcore::err& Err )
        {
            int Index = int(I);
            if( Err = File.Field( "Index",        Index ) ) return;
            
            auto& Bone = m_Bone[Index];
               (Err = File.Field( "Name",         Bone.m_Name )                                                                                                 )
            || (Err = File.Field( "nChildren",    Bone.m_nChildren)                                                                                             )
            || (Err = File.Field( "iParent",      Bone.m_iParent)                                                                                               )
            || (Err = File.Field( "Scale",        Bone.m_BindScale.m_X,       Bone.m_BindScale.m_Y,       Bone.m_BindScale.m_Z)                                 )
            || (Err = File.Field( "Rotate",       Bone.m_BindRotation.m_X,    Bone.m_BindRotation.m_Y,    Bone.m_BindRotation.m_Z,    Bone.m_BindRotation.m_W ) )
            || (Err = File.Field( "Pos",          Bone.m_BindTranslation.m_X, Bone.m_BindTranslation.m_Y, Bone.m_BindTranslation.m_Z)                           )
            || (Err = File.Field( "bScaleKeys",   Bone.m_bScaleKeys)                                                                                            )
            || (Err = File.Field( "bRotKeys",     Bone.m_bRotationKeys)                                                                                         )
            || (Err = File.Field( "bPosKeys",     Bone.m_bTranslationKeys)                                                                                      )
            ;
        })
      ) throw(std::runtime_error(Error.getCode().m_pString));


    if( File.Record
        (Error
        , "KeyFrames"
        , [&](std::size_t& C, xcore::err& Err)
        {
            if (isRead) Err = m_KeyFrame.New(C);
            else        C   = m_KeyFrame.size();
        }
        , [&](std::size_t I, xcore::err& Err)
        {
            int Index = int(I);
            if (Err = File.Field("iKey", Index)) return;

            int iBone  = Index % m_Bone.size<int>();
            if (Err = File.Field("iBone", iBone)) return;

            int iFrame = Index / m_Bone.size<int>();
            if (Err = File.Field("iFrame", iFrame)) return;

            auto& Frame = m_KeyFrame[Index];
               (Err = File.Field("Scale",     Frame.m_Scale.m_X,       Frame.m_Scale.m_Y,       Frame.m_Scale.m_Z )                          )
            || (Err = File.Field("Rotate",    Frame.m_Rotate.m_X,      Frame.m_Rotate.m_Y,      Frame.m_Rotate.m_Z,       Frame.m_Rotate.m_W ) )
            || (Err = File.Field("Translate", Frame.m_Translate.m_X,   Frame.m_Translate.m_Y,   Frame.m_Translate.m_Z )                      )
            ;
        })
      ) throw(std::runtime_error(Error.getCode().m_pString));
}

//--------------------------------------------------------------------------

void anim::ComputeBonesL2W( xcore::matrix4* pMatrix, float Frame ) const
{
    std::int32_t i;

    // Keep frame in range
    Frame = std::fmodf( Frame,float(m_nFrames-1) );

    // Compute integer and 
    std::int32_t iFrame0 = (std::int32_t)Frame;
    std::int32_t iFrame1 = (iFrame0+1)%m_nFrames;
    float fFrame  = Frame - iFrame0;

    // Loop through bones and build matrices
    const key_frame* pF0 = &m_KeyFrame[ iFrame0*m_Bone.size<int>() ];
    const key_frame* pF1 = &m_KeyFrame[ iFrame1*m_Bone.size<int>() ];
    for( i=0; i<m_Bone.size<int>(); i++ )
    {
        xcore::quaternion R = pF0->m_Rotate.BlendAccurate( fFrame, pF1->m_Rotate );
        xcore::vector3    S = pF0->m_Scale     + fFrame*(pF1->m_Scale       - pF0->m_Scale);
        xcore::vector3    T = pF0->m_Translate + fFrame*(pF1->m_Translate - pF0->m_Translate);
        pF0++;
        pF1++;
        pMatrix[i].setup( S, R, T );

        // Concatenate with parent
        if( m_Bone[i].m_iParent != -1 )
        {
            xassert( m_Bone[i].m_iParent < i );
            pMatrix[i] = pMatrix[ m_Bone[i].m_iParent ] * pMatrix[i];
        }
    }

    // Apply bind matrices
    for( i=0; i<m_Bone.size(); i++ )
    {
        pMatrix[i] = pMatrix[i] * m_Bone[i].m_BindMatrixInv;
    }
}

//--------------------------------------------------------------------------

void anim::ComputeBonesL2W( xcore::matrix4* pMatrix
                          , std::int32_t    iFrame
                          , bool            bRemoveHorizMotion
                          , bool            bRemoveVertMotion
                          , bool            bRemoveYawMotion ) const
{
    std::int32_t i;

    // Keep frame in range
    iFrame = iFrame % (m_nFrames-1) ;

    // Loop through bones and build matrices
    const key_frame* pF0 = &m_KeyFrame[ iFrame * m_Bone.size<int>() ];
    for( i=0; i<m_Bone.size<int>(); i++ )
    {
        // Root bone mayhem?
        if (i == 0)
        {
            // Lookup info
            xcore::vector3     Scale = pF0->m_Scale ;
            xcore::vector3     Trans = pF0->m_Translate ;
            xcore::quaternion  Rot   = pF0->m_Rotate ;

            // Remove horiz motion?
            if( bRemoveHorizMotion )
                Trans.m_X = Trans.m_Z = 0.0f ;

            // Remove vert motion?
            if( bRemoveVertMotion )
                Trans.m_Y = 0.0f ;

            // Remove yaw motion?
            if( bRemoveYawMotion )
            {
                // TO DO...
            }

            // Setup matrix from frame
            pMatrix[i].setup( Scale, Rot, Trans );
        }
        else
        {
            // Setup matrix from frame
            pMatrix[i].setup( pF0->m_Scale, pF0->m_Rotate, pF0->m_Translate );
        }

        // Next frame
        pF0++;

        // Concatenate with parent
        if( m_Bone[i].m_iParent != -1 )
            pMatrix[i] = pMatrix[ m_Bone[i].m_iParent ] * pMatrix[i];
    }

    // Apply bind matrices
    for( i=0; i<m_Bone.size<int>(); i++ )
    {
        pMatrix[i] = pMatrix[i] * m_Bone[i].m_BindMatrixInv;
    }
}

//--------------------------------------------------------------------------

void anim::ComputeBoneL2W( std::int32_t iBone, xcore::matrix4& Matrix, float Frame ) const
{
    // Keep frame in range
    Frame = std::fmodf( Frame, float( m_nFrames - 1) );

    // Compute integer and 
    std::int32_t iFrame0 = (std::int32_t)Frame;
    std::int32_t iFrame1 = (iFrame0+1)%m_nFrames;
    float fFrame  = Frame - iFrame0;

    // Loop through bones and build matrices
    const key_frame* pF0 = &m_KeyFrame[ iFrame0 * m_Bone.size<int>() ];
    const key_frame* pF1 = &m_KeyFrame[ iFrame1 * m_Bone.size<int>() ];

    // Clear bone matrix
    Matrix.identity();

    // Run hierarchy from bone to root node
    std::int32_t I = iBone;
    while( I != -1 )
    {
        xcore::quaternion R = pF0[I].m_Rotate.BlendAccurate( fFrame, pF1[I].m_Rotate );
        xcore::vector3    S = pF0[I].m_Scale       + fFrame*(pF1[I].m_Scale       - pF0[I].m_Scale);
        xcore::vector3    T = pF0[I].m_Translate + fFrame*(pF1[I].m_Translate - pF0[I].m_Translate);

        xcore::matrix4 LM;
        LM.setup( S, R, T );

        Matrix = LM * Matrix;
        I = m_Bone[I].m_iParent;
    }

    // Apply bind matrix
    Matrix = Matrix * m_Bone[iBone].m_BindMatrixInv;
}

//--------------------------------------------------------------------------

void anim::ComputeRawBoneL2W( std::int32_t iBone, xcore::matrix4& Matrix, std::int32_t iFrame ) const
{
    // Keep frame in range
    xassert( (iFrame>=0) && (iFrame<m_nFrames) );

    // Loop through bones and build matrices
    const key_frame* pF = &m_KeyFrame[ iFrame * m_Bone.size<int>() ];

    // Clear bone matrix
    Matrix.identity();

    // Run hierarchy from bone to root node
    std::int32_t I = iBone;
    while( I != -1 )
    {
        xcore::quaternion R = pF[I].m_Rotate;
        xcore::vector3    S = pF[I].m_Scale;
        xcore::vector3    T = pF[I].m_Translate;

        xcore::matrix4 LM;
        LM.setup( S, R, T );

        Matrix = LM * Matrix;
        I = m_Bone[I].m_iParent;
    }

    // Apply bind matrix
    Matrix = Matrix * m_Bone[iBone].m_BindMatrixInv;
}

//--------------------------------------------------------------------------

void anim::ComputeBoneKeys( xcore::quaternion* pQ, xcore::vector3* pS, xcore::vector3* pT, float Frame ) const
{
    std::int32_t i;

    // Keep frame in range
    Frame = std::fmodf(Frame,(float)(m_nFrames-1));

    // Compute integer and 
    std::int32_t iFrame0 = (std::int32_t)Frame;
    std::int32_t iFrame1 = (iFrame0+1)%m_nFrames;
    float fFrame  = Frame - iFrame0;

    // Loop through bones and build matrices
    const key_frame* pF0 = &m_KeyFrame[ iFrame0 * m_Bone.size<int>() ];
    const key_frame* pF1 = &m_KeyFrame[ iFrame1 * m_Bone.size<int>() ];
    for( i=0; i<m_Bone.size<int>(); i++ )
    {
        pQ[i] = pF0->m_Rotate.BlendAccurate( fFrame, pF1->m_Rotate );
        pS[i] = pF0->m_Scale       + fFrame*(pF1->m_Scale       - pF0->m_Scale);
        pT[i] = pF0->m_Translate + fFrame*(pF1->m_Translate - pF0->m_Translate);
        pF0++;
        pF1++;
    }
}

//--------------------------------------------------------------------------

std::int32_t anim::GetBoneIDFromName( const char* pBoneName ) const
{
    std::int32_t i;
    for( i=0; i<m_Bone.size<int>(); i++ )
    if( xcore::string::CompareI( pBoneName, m_Bone[i].m_Name ) == 0 )
        return i;
    return -1;
}

//--------------------------------------------------------------------------

void anim::RemoveFramesFromRage( std::int32_t StartingValidRange, std::int32_t EndingValidRange )
{
    xassert( StartingValidRange >= 0 );
    xassert( EndingValidRange >= 0 );
    xassert( EndingValidRange <= m_nFrames );
    xassert( StartingValidRange <= m_nFrames );

    if( StartingValidRange == 0 && EndingValidRange == m_nFrames )
        return;

    xcore::unique_span<key_frame> NewRange;

    const std::int32_t nFrames = EndingValidRange - StartingValidRange;

    if( auto Err = NewRange.New( nFrames * m_Bone.size<int>() ); Err ) throw(std::runtime_error(Err.getCode().m_pString));
    for( std::int32_t i = StartingValidRange; i <EndingValidRange; i++ )
    {
        const std::int32_t iNewSpace = i - StartingValidRange;
        std::memcpy( &NewRange[ iNewSpace * m_Bone.size<int>() ], &m_KeyFrame[ i * m_Bone.size<int>()], sizeof(key_frame) * m_Bone.size<int>() );
    }     

    // Set the new key frames
    m_KeyFrame = std::move(NewRange);
    m_nFrames  = nFrames;
}

//--------------------------------------------------------------------------

void anim::BakeBindingIntoFrames( bool DoScale, bool DoRotation, bool DoTranslation )
{
    std::int32_t i,j;

    //
    // Loop through frames of animation
    //
    xcore::unique_span<xcore::matrix4> L2W;
    if( auto Err = L2W.New( m_Bone.size<int>() ); Err ) throw(std::runtime_error(Err.getCode().m_pString));

    for( i=0; i<m_nFrames; i++ )
    {
        //
        // Compute matrices for current animation.
        // No binding is applied
        //
        for( j=0; j<m_Bone.size<int>(); j++ )
        {
            key_frame* pF = &m_KeyFrame[ i*m_Bone.size<int>() + j ];

            L2W[j].setup( pF->m_Scale, pF->m_Rotate, pF->m_Translate );

            // Concatenate with parent
            if( m_Bone[j].m_iParent != -1 )
            {
                L2W[j] = L2W[m_Bone[j].m_iParent] * L2W[j];
            }
        }

        //
        // Apply original bind matrices
        //
        for( j=0; j<m_Bone.size<int>(); j++ )
        {
            L2W[j] = L2W[j] * m_Bone[j].m_BindMatrixInv;
        }

        //
        // Remove bind translation and scale matrices
        //
        for( j=0; j<m_Bone.size<int>(); j++ )
        {
            xcore::quaternion R = m_Bone[j].m_BindRotation;
            xcore::vector3    S = m_Bone[j].m_BindScale;
            xcore::vector3    T = m_Bone[j].m_BindTranslation;

            if( DoScale )       S.setup(1,1,1);
            if( DoRotation )    R.identity();
            if( DoTranslation ) T.setup(0,0,0);

            xcore::matrix4 BindMatrix;
            BindMatrix.setup( S, R, T );
            L2W[j] = L2W[j] * BindMatrix;
        }

        // Convert back to local space transform
        for( j = m_Bone.size<int>()-1; j>0; j-- )
            if( m_Bone[j].m_iParent != -1 )
            {
                xcore::matrix4 PM = L2W[ m_Bone[j].m_iParent ];
                PM.InvertSRT();
                L2W[j] = PM * L2W[j];
            }

        // Pull out rotation scale and translation
        for( j=0; j<m_Bone.size<int>(); j++ )
        {
            key_frame* pF       = &m_KeyFrame[i * m_Bone.size<int>() + j ];
            
            pF->m_Scale         = L2W[j].getScale();
            pF->m_Rotate.setup( L2W[j] );
            pF->m_Translate   = L2W[j].getTranslation();
            
        }
    }

    // Remove wanted attributes out of the binding
    for( i=0; i<m_Bone.size<int>(); i++ )
    {
        if ( DoTranslation )
            m_Bone[ i ].m_BindTranslation.setup(0);

        if( DoScale ) 
            m_Bone[i].m_BindScale.setup(1,1,1);

        if( DoRotation )
            m_Bone[i].m_BindRotation.identity();

        m_Bone[i].m_BindMatrix.setup( m_Bone[i].m_BindScale, m_Bone[i].m_BindRotation, m_Bone[i].m_BindTranslation );
        m_Bone[i].m_BindMatrixInv = m_Bone[i].m_BindMatrix;
        m_Bone[i].m_BindMatrixInv.InvertSRT();
    }
}

//--------------------------------------------------------------------------

void anim::DeleteDummyBones( void )
{
    std::int32_t iBone = 0;
    while( iBone < m_Bone.size<int>() )
    {
        string S( m_Bone[iBone].m_Name );

        if( xcore::string::FindStr( S, "dummy") != -1)
        {
            //Check if it is the root.  If it is, make sure it is not the only root; that is,
            // we can only delete a root bone if it only has one child (because then its child
            // can become the new root)
            if(m_Bone[iBone].m_iParent == -1)
            {
                std::int32_t nChildren = 0;
                for(std::int32_t count = 0; count < m_Bone.size<int>(); count++)
                {
                    if( m_Bone[count].m_iParent == iBone )
                        nChildren++;
                }

                if(nChildren == 1)
                {
                    //x_DebugMsg("Bone is root, but can be removed: '%s'\n", m_pBone[iBone].Name);                
                    DeleteBone(iBone);
                    iBone = 0;
                }
                else
                {
                    //x_DebugMsg("Bone is sole remaining root: '%s'\n", m_pBone[iBone].Name);
                    iBone++;
                }
            }
            else
            {
                DeleteBone(iBone);
                iBone = 0;
            }
        }
        else
        {
            iBone++;
        }
    }

/*
    for(iBone = 0; iBone < m_nBones; iBone++)
    {
        x_DebugMsg("Bone Index: %3d Parent: %3d Name: '%s'\n", iBone, m_pBone[iBone].iParent, m_pBone[iBone].Name);
    }
*/
}
    
//--------------------------------------------------------------------------

void anim::DeleteBone( const char* pBoneName )
{
    std::int32_t iBone = this->GetBoneIDFromName(pBoneName);
    if(iBone != -1)
        DeleteBone(iBone);
    return;
}

//--------------------------------------------------------------------------

void anim::DeleteBone( std::int32_t iBone )
{
    //x_DebugMsg("Deleting bone: '%s'\n", m_pBone[iBone].Name);
    std::int32_t i,j;
    //xassertS( m_Bone.size<int>() > 1, TempDebugFileName );

    //
    // Allocate new bones and frames
    //
    std::int32_t                 nNewBones = m_Bone.size<int>()-1;
    xcore::unique_span<bone>          NewBone;
    xcore::unique_span<key_frame>     NewFrame;
    
    if( auto Err = NewBone.New( nNewBones );              Err ) throw(std::runtime_error(Err.getCode().m_pString));
    if (auto Err = NewFrame.New( nNewBones * m_nFrames ); Err ) throw(std::runtime_error(Err.getCode().m_pString));
    
    //
    // Check and see if bone has any children
    //
    bool HasChildren = false;
    for( i=0; i<m_Bone.size<int>(); i++ )
        if( m_Bone[i].m_iParent == iBone )
            HasChildren = true;

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
        for( i=0; i<nNewBones; i++ )
            if( NewBone[i].m_iParent == iBone )
            {
                NewBone[i].m_iParent = m_Bone[iBone].m_iParent;
            }

        // Patch references to any bone > iBone
        for( i=0; i<nNewBones; i++ )
            if( NewBone[i].m_iParent > iBone )
            {
                NewBone[i].m_iParent--;
            }
    }


    // 
    // If there were no children then we can quickly copy over the keys
    //
    if( !HasChildren )
    {
        //
        // Loop through frames of animation
        //
        std::int32_t k=0;
        for( i=0; i<m_nFrames; i++ )
        for( j=0; j<m_Bone.size<int>(); j++ )
        {
            if( j!=iBone )
                NewFrame[k++] = m_KeyFrame[ i*m_Bone.size<int>() + j ];
        }
    }
    else
    {
        //
        // Loop through frames of animation
        //
        xcore::unique_span<xcore::matrix4> L2W;
        if( auto Err = L2W.Alloc( m_Bone.size<int>() ); Err ) throw(std::runtime_error(Err.getCode().m_pString));

        for( i=0; i<m_nFrames; i++ )
        {
            // Compute matrices for current animation.
            for( j=0; j<m_Bone.size<int>(); j++ )
            {
                const key_frame* pF = &m_KeyFrame[ i*m_Bone.size<int>()+j ];

                L2W[j].setup( pF->m_Scale, pF->m_Rotate, pF->m_Translate );

                // Concatenate with parent
                if( m_Bone[j].m_iParent != -1 )
                {
                    L2W[j] = L2W[ m_Bone[j].m_iParent ] * L2W[j];
                }
            }

            // Apply original bind matrices
            //for( j=0; j<m_Bone.size<int>(); j++ )
            //{
            //    L2W[j] = L2W[j] * m_Bone[j].m_BindMatrixInv;
            //}

            // Shift bones down to align with NewBones
            if( iBone != (m_Bone.size<int>()-1) ) 
                std::memmove( &L2W[iBone], &L2W[ iBone+1 ], sizeof(xcore::matrix4)*(m_Bone.size<int>()-iBone-1) );

            //for( j=iBone+1; j<m_Bone.size<int>(); j++ )
            //    L2W[j-1] = L2W[j];


            // Remove bind translation and scale matrices
            //for( j=0; j<nNewBones; j++ )
            //{
            //    L2W[j] = L2W[j] * NewBone[j].m_BindMatrix;
            //}

            // Convert back to local space transform
            for( j=nNewBones-1; j>0; j-- )
                if( NewBone[j].m_iParent != -1 )
                {
                    xcore::matrix4 PM = L2W[ NewBone[j].m_iParent ];
                    PM.InvertSRT();
                    L2W[j] = PM * L2W[j];
                }

            // Pull out rotation scale and translation
            for( j=0; j<nNewBones; j++ )
            {
                key_frame* pF       = &NewFrame[i*nNewBones + j];
                
                pF->m_Scale = L2W[j].getScale();
                pF->m_Rotate.setup( L2W[j] );
                pF->m_Translate = L2W[j].getTranslation();
            }
        }
    }

    // free current allocations
    m_Bone.clear();
    m_KeyFrame.clear();

    m_Bone     = std::move(NewBone);
    m_KeyFrame = std::move(NewFrame);
}

//--------------------------------------------------------------------------

bool anim::ApplyNewSkeleton( const anim& BindAnim )
{
    std::int32_t i,j,k;
    bool Problem=false;

    //
    // Handle setting the new root
    //
    {
        xassert( -1 == BindAnim.m_Bone[0].m_iParent );
        std::int32_t iRoot = GetBoneIDFromName( BindAnim.m_Bone[0].m_Name );
        if( iRoot == -1 )
            throw(std::runtime_error("ERROR: Failt to apply the new skeleton because I was not able to find the rootbone in the old skeleton"));

        // Set the new root first then we can deal with more complex bone removal stuff
        if( iRoot != 0 )
            setNewRoot( iRoot );
    }

    //
    // Remove all bones not in BindAnim
    //
    i = 0;
    while( i < m_Bone.size<int>() )
    {
        for( j=0; j<BindAnim.m_Bone.size<int>(); j++ )
        if( xcore::string::CompareI( m_Bone[i].m_Name, BindAnim.m_Bone[j].m_Name ) == 0 )
            break;

        if( j == BindAnim.m_Bone.size<int>() )
        {
            if( m_Bone.size<int>() == 1 )
            {
                throw(std::runtime_error( "ERROR: has no bones in bind." ));
            }

            DeleteBone( i );
            i=-1;
        }

        i++;
    }


    //
    // Allocate new bones and frames
    //
    xcore::unique_span<bone>        NewBone;
    xcore::unique_span<key_frame>   NewFrame;

    if( auto Err = NewBone.New( BindAnim.m_Bone.size<int>() );              Err ) throw(std::runtime_error(Err.getCode().m_pString));
    if( auto Err = NewFrame.New( BindAnim.m_Bone.size<int>() * m_nFrames ); Err ) throw(std::runtime_error(Err.getCode().m_pString));
    
    //
    // Copy over bind skeleton
    //
    std::memcpy( NewBone.data(), BindAnim.m_Bone.data(), sizeof(bone)*BindAnim.m_Bone.size<int>() );

    //
    // Construct frames
    //
    for( i=0; i<BindAnim.m_Bone.size<int>(); i++ )
    {
        // Lookup bone in current anim
        for( j=0; j<m_Bone.size<int>(); j++ )
            if( xcore::string::CompareI( m_Bone[j].m_Name, NewBone[i].m_Name ) == 0 )
                break;

        // Check if bone is present
        if( j == m_Bone.size<int>() )
        {
            Problem = true;

            // No bone present.  
            // Copy over first frame of BindAnim

            key_frame Key;
            Key.m_Rotate = BindAnim.m_Bone[i].m_BindRotation;
            Key.m_Scale    = BindAnim.m_Bone[ i ].m_BindScale;
            Key.m_Translate.setup(0);

            for( k=0; k<m_nFrames; k++ )
                NewFrame[k*BindAnim.m_Bone.size<int>() + i] = Key; //.identity();// = BindAnim.m_KeyFrame[i];
        }
        else
        {
            // Copy IsLayer over to new bones
            NewBone[i].m_bIsMasked = m_Bone[j].m_bIsMasked;

            // Copy data into new bone slot
            for( k=0; k<m_nFrames; k++ )
                NewFrame[k*BindAnim.m_Bone.size<int>() + i] = m_KeyFrame[k*m_Bone.size<int>()+j];
        }
    }

    //
    // Free current allocations and provide new ones
    //
    m_Bone.clear();
    m_KeyFrame.clear();
    
    m_Bone      = std::move(NewBone);
    m_KeyFrame  = std::move(NewFrame);

    return !Problem;
}

//--------------------------------------------------------------------------

bool anim::HasSameSkeleton( const anim& Anim ) const
{
    std::int32_t i;

    if( m_Bone.size<int>() != Anim.m_Bone.size<int>() )
        return false;

    for( i=0; i<m_Bone.size<int>(); i++ )
    {
        const bone& B0 = m_Bone[i];
        const bone& B1 = Anim.m_Bone[i];

        if( xcore::string::CompareI( B0.m_Name, B1.m_Name ) != 0 )
            return false;

        if( B0.m_iParent != B1.m_iParent )
            return false;

        if( B0.m_nChildren != B1.m_nChildren )
            return false;

        for( std::int32_t j=0; j<4*4; j++ )
        {
            float D = std::abs( ((float*)&B0)[j] - ((float*)&B1)[j] );
            if( D > 0.0001f )
                return false;
        }
    }

    return true;
}

//--------------------------------------------------------------------------

void anim::SanityCheck( void ) const
{
    xassert( (m_Bone.size<int>()>0) && (m_Bone.size<int>()<2048) );
    xassert( (m_nFrames>=0) && (m_nFrames<65536) );
}

//--------------------------------------------------------------------------

bool anim::isMaskedAnim( void ) const
{
    for( std::int32_t i=0; i<m_Bone.size<int>(); i++ )
        if( m_Bone[i].m_bIsMasked )
            return true;

    return false;
}

//--------------------------------------------------------------------------

void anim::CleanUp( void )
{
    PutBonesInLODOrder();
    DeleteDummyBones();
}

//--------------------------------------------------------------------------

void anim::setNewRoot( std::int32_t Index )
{

    //
    // Allocate new bones and frames
    //
    const std::int32_t           nParentsBones   = Index;
    const std::int32_t           nNewBones       = m_Bone.size<int>() - nParentsBones;
    xcore::unique_span<bone>          NewBone;
    xcore::unique_span<key_frame>     NewFrame;

    if( auto Err = NewBone.New( nNewBones ); Err ) throw(std::runtime_error(Err.getCode().m_pString));
    if( auto Err = NewFrame.New( nNewBones * m_nFrames ); Err ) throw(std::runtime_error(Err.getCode().m_pString));

    //
    // Build new hierarchy
    //
    {
        // Copy over remaining bones
        for( std::int32_t i=0; i<nNewBones; i++ )
            NewBone[i] = m_Bone[ Index + i ];

        // Patch children of bone
        for ( std::int32_t i = 1; i < nNewBones; i++ )
            if ( NewBone[ i ].m_iParent < Index )
                throw(std::runtime_error("ERROR: While setting the new root bone I found children accessing its parents"));
          
        // Patch references to any bone > Index
        NewBone[ 0 ].m_iParent = -1;
        for ( std::int32_t i = 1; i<nNewBones; i++ )
        {
            NewBone[ i ].m_iParent -= nParentsBones;
            xassert( NewBone[ i ].m_iParent >=0 );
        }
    }

    //
    // Loop through frames of animation
    //
    xcore::unique_span<xcore::matrix4> L2W;
    if( auto Err = L2W.Alloc( m_Bone.size<int>() ); Err ) throw(std::runtime_error(Err.getCode().m_pString));

    for ( std::int32_t iFrame = 0; iFrame < m_nFrames; iFrame++ )
    {
        // Compute matrices for current animation.
        for ( std::int32_t j = 0; j < m_Bone.size<int>(); j++ )
        {
            const key_frame& F = m_KeyFrame[ iFrame*m_Bone.size<int>() + j ];

            L2W[ j ].setup( F.m_Scale, F.m_Rotate, F.m_Translate );

            // Concatenate with parent
            if ( m_Bone[ j ].m_iParent != -1 )
            {
                L2W[ j ] = L2W[ m_Bone[ j ].m_iParent ] * L2W[ j ];
            }
        }

        // Shift bones down to align with NewBones
        std::memmove( &L2W[0], &L2W[ Index ], sizeof(xcore::matrix4)*nNewBones );

        // Convert back to local space transform
        for ( std::int32_t j = nNewBones - 1; j>0; j-- )
        {
            xassert( NewBone[ j ].m_iParent != -1 );
            xcore::matrix4 PM = L2W[ NewBone[ j ].m_iParent ];
            PM.InvertSRT();
            L2W[ j ] = PM * L2W[ j ];
        }

        // Pull out rotation scale and translation
        for (std::int32_t  j = 0; j < nNewBones; j++ )
        {
            key_frame& Frame = NewFrame[ iFrame*nNewBones + j ];

            Frame.m_Scale = L2W[ j ].getScale();
            Frame.m_Rotate.setup( L2W[ j ] );
            Frame.m_Translate = L2W[ j ].getTranslation();

        }
    }

    // set the new data
    m_Bone      = std::move(NewBone);
    m_KeyFrame  = std::move(NewFrame);
}


//--------------------------------------------------------------------------

void anim::CopyFrames( xcore::unique_span<key_frame>& KeyFrame, std::int32_t iStart, std::int32_t nFrames ) const
{
    xassert( iStart >= 0 );
    xassert( nFrames > 0 );
    xassert( iStart + nFrames <= m_nFrames );

    const std::int32_t nBones = m_Bone.size<int>();
    if( auto Err = KeyFrame.Alloc( nBones * nFrames ); Err ) throw(std::runtime_error(Err.getCode().m_pString));
    std::memcpy( &KeyFrame[0], &m_KeyFrame[ iStart * nBones ], sizeof(key_frame)*nBones*nFrames );
}

//--------------------------------------------------------------------------

void anim::InsertFrames( std::int32_t iDestFrame, xcore::unique_span<key_frame>& KeyFrame )
{
    xassert( iDestFrame >= 0 );
    if( KeyFrame.size<int>() == 0 )
        return;

    xassert( KeyFrame.size<int>() > 0 );
    xassert( iDestFrame <= m_nFrames );

    const std::int32_t nBones        = m_Bone.size<int>();
    const std::int32_t oldnFrames    = m_nFrames;

    // Update the number of frames in the anim
    m_nFrames += KeyFrame.size<int>()/nBones;

    if( m_KeyFrame.size<int>() == 0 )
    {
        if( auto Err = m_KeyFrame.copy( KeyFrame );  Err ) throw(std::runtime_error(Err.getCode().m_pString));
        return;
    }

    if( auto Err = m_KeyFrame.resize( m_KeyFrame.size<int>() + KeyFrame.size<int>() ); Err ) throw(std::runtime_error(Err.getCode().m_pString));
    
    if( iDestFrame != oldnFrames )
    {
        std::memmove( &m_KeyFrame[ iDestFrame * nBones + KeyFrame.size<int>() ], 
                   &m_KeyFrame[ iDestFrame * nBones ], 
                   sizeof(key_frame)*KeyFrame.size<int>() );
    }

    std::memcpy( &m_KeyFrame[ iDestFrame * nBones ], &KeyFrame[0], sizeof(key_frame)*KeyFrame.size<int>() );
}

//--------------------------------------------------------------------------

void anim::RencenterAnim( bool TX, bool TY, bool TZ, bool Pitch, bool Yaw, bool Roll )
{
    if( TX | TY | TZ )
    {
        const std::int32_t       nBones          =   m_Bone.size<int>();
        const xcore::vector3  LinearVelocity  =   m_KeyFrame[ nBones * 1 ].m_Translate -
                                            m_KeyFrame[ nBones * 0 ].m_Translate;
        const xcore::vector3  CurrentCenter   =   m_KeyFrame[ nBones * 0 ].m_Translate;
         xcore::vector3       NewCenterDelta(0);

        if( TX )
            NewCenterDelta.m_X = LinearVelocity.m_X - CurrentCenter.m_X;

        if ( TY )
            NewCenterDelta.m_Y = LinearVelocity.m_Y - CurrentCenter.m_Y;

        if ( TZ )
            NewCenterDelta.m_Z = LinearVelocity.m_Z - CurrentCenter.m_Z;

        for( std::int32_t i=0; i<m_nFrames; i++ )
        {
            auto& Frame = m_KeyFrame[ nBones * i ]; 
            Frame.m_Translate += NewCenterDelta; 
        }
    }

    //
    // Re-aligned Rotation
    //
    if( Pitch | Yaw | Roll )
    {
        const std::int32_t nBones          = m_Bone.size<int>();
        auto      InvRotation     = xcore::quaternion( m_KeyFrame[ 0 ].m_Rotate ).Invert().getRotation();

        if( !Pitch ) InvRotation.m_Pitch = 0_xdeg;
        if( !Yaw )   InvRotation.m_Yaw   = 0_xdeg;
        if( !Roll )  InvRotation.m_Roll  = 0_xdeg;

        const xcore::quaternion InvRotFiltered( InvRotation );

        for( std::int32_t i=0; i<m_nFrames; i++ )
        {
            auto& Frame = m_KeyFrame[ nBones * i ]; 
            Frame.m_Rotate    = InvRotFiltered * Frame.m_Rotate;
            Frame.m_Translate = InvRotFiltered * Frame.m_Translate;
        }
    }
}

//--------------------------------------------------------------------------

void anim::CleanLoopingAnim( void )
{
    const std::int32_t                      nFrames         = m_nFrames;
    const std::int32_t                      nBones          = m_Bone.size<int>();
    const std::int32_t                      nAffectedFrames = m_FPS / 19;
    xcore::unique_span<xcore::transform3>   ContinuesFrames;
    xcore::unique_span<xcore::transform3>   NewFrames;

    //
    // Create the array of the new frames and fill it with information
    //
    if( auto Err = NewFrames.New( nAffectedFrames * 2 * nBones );       Err ) throw(std::runtime_error(Err.getCode().m_pString));
    if( auto Err = ContinuesFrames.New( nAffectedFrames * 2 * nBones ); Err ) throw(std::runtime_error(Err.getCode().m_pString));
     
    //
    // Create the frames in the same loop space
    //
    {
        const auto&             RootLastFrame   = m_KeyFrame[ (nFrames-1) * nBones ];
        //const auto&           RootFirstFrame  = m_KeyFrame[ 0 ];
        const auto              DeltaYaw        = RootLastFrame.m_Rotate.getRotation().m_Yaw;
        const xcore::quaternion DeltaRot        = xcore::quaternion( xcore::radian3(0_xdeg,DeltaYaw,0_xdeg) );

        for( std::int32_t i=0; i<nAffectedFrames*2; i++ )
        {
            const std::int32_t       iFrame    = ( nFrames - (nAffectedFrames - i) ) % nFrames;
            auto&           NewFrame  =  ContinuesFrames[ i * nBones ];
            const auto&     Frame     =  m_KeyFrame[iFrame * nBones ];

            // Copy one frame worth of data
            std::memcpy( &NewFrame, &Frame, sizeof(xcore::transform3) * nBones );

            // Set the base frames to be in the space of the space of the last frame loop
            if( iFrame < nAffectedFrames )
            {
                NewFrame.m_Rotate     = DeltaRot * Frame.m_Rotate;

                const xcore::vector3 RelTrans = DeltaRot * Frame.m_Translate;

                NewFrame.m_Translate = RootLastFrame.m_Translate + RelTrans;
                NewFrame.m_Translate.m_Y = Frame.m_Translate.m_Y;
            }
        }
    }

    //
    // Interpolate Frames
    //
    {
        const std::int32_t iFrame0 = 0;
        const std::int32_t iFrame1 = (nAffectedFrames*2)-1;
        for( std::int32_t i=0; i<nAffectedFrames*2; i++ )
        {
            float T = (i + 0.5f)/(float)(nAffectedFrames*2);

            // Blend Frames
            for( std::int32_t b=0;b<nBones;b++)
            {
                const auto      KeyFrame0    =  ContinuesFrames[ iFrame0 * nBones + b ];
                const auto      KeyFrame1    =  ContinuesFrames[ iFrame1 * nBones + b ];
                auto&           KeyFrameDest =  NewFrames[ i       * nBones + b ];
            
                KeyFrameDest.Blend( KeyFrame0, T, KeyFrame1 );
            }
        }
    }

    //
    // New Loops interpolates between the new computed frames and the blends
    //
    for( std::int32_t j=0; j<nAffectedFrames; j++ )
    {
        const std::int32_t nNewAffectedFrames = nAffectedFrames-j;
        const std::int32_t iFrame0 = j;
        const std::int32_t iFrame1 = (nNewAffectedFrames*2)-1;
    
        for( std::int32_t i=0; i<nNewAffectedFrames*2; i++ )
        {
            float T = (i + 0.5f)/(float)(nNewAffectedFrames*2);

            // Blend Frames
            for( std::int32_t b=0;b<nBones;b++)
            {
                const auto&         KeyFrame0     =  ContinuesFrames[ iFrame0 * nBones + b ];
                const auto&         KeyFrame1     =  ContinuesFrames[ iFrame1 * nBones + b ];
                xcore::transform3   KeyFrameDest;

                KeyFrameDest.Blend( KeyFrame0, T, KeyFrame1);
                
                auto&            KeyFrameFinal = NewFrames[ (j+i) * nBones + b ];

                KeyFrameFinal.Blend( 0.5, KeyFrameDest );
            }
        }
    }

    //
    // Save back the frames
    //
    {
        const auto              LastNewFrame  = NewFrames[ (nAffectedFrames-1) * nBones ];
        const auto              FirstNewFrame = NewFrames[ nAffectedFrames * nBones ];  
        const auto              DeltaYaw      = LastNewFrame.m_Rotate.getRotation().m_Yaw; 
        const xcore::quaternion DeltaRot      = xcore::quaternion( xcore::radian3(0_xdeg,-DeltaYaw,0_xdeg) );

        for( std::int32_t i=0; i<nAffectedFrames*2; i++ )
        {
            const std::int32_t  iFrame      = ( nFrames - (nAffectedFrames - i) ) % nFrames;
            auto&               NewFrame    =  NewFrames[ i * nBones ];
            auto&               Frame       =  m_KeyFrame[iFrame * nBones ];

            if( iFrame < nAffectedFrames )
            {
                NewFrame.m_Rotate     = DeltaRot * NewFrame.m_Rotate;

                xcore::vector3 Trans    = NewFrame.m_Translate - LastNewFrame.m_Translate;
                Trans.m_Y = NewFrame.m_Translate.m_Y;

                NewFrame.m_Translate = DeltaRot * Trans;
            }

            // Copy one frame worth of data
            std::memcpy( &Frame, &NewFrame, sizeof(xcore::transform3) * nBones );
        }
    }
}


} // namespace xraw3d