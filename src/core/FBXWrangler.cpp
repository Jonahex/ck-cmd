/*
BodySlide and Outfit Studio
Copyright (C) 2018  Caliente & ousnius
See the included LICENSE file
*/

#include <core/FBXWrangler.h>
#include <core/EulerAngles.h>
#include <core/MathHelper.h>
#include <commands/Geometry.h>
#include <commands/NifScan.h>
#include <core/log.h>

#include <Physics\Utilities\Collide\ShapeUtils\CreateShape\hkpCreateShapeUtility.h>
#include <Common\GeometryUtilities\Misc\hkGeometryUtils.h>
#include <Physics\Utilities\Collide\ShapeUtils\ShapeConverter\hkpShapeConverter.h>
#include <Physics/Collide/Shape/Convex/Box/hkpBoxShape.h>
#include <Physics/Collide/Shape/Convex/Capsule/hkpCapsuleShape.h>
#include <Common\Base\Types\Geometry\hkGeometry.h>
#include <Physics\Collide\Shape\Convex\ConvexVertices\hkpConvexVerticesShape.h>
#include <Common\Base\Types\Geometry\hkStridedVertices.h>
#include <Physics\Collide\Shape\Convex\Sphere\hkpSphereShape.h>

//// Physics
#include <Physics/Dynamics/Entity/hkpRigidBody.h>
#include <Physics/Collide/Shape/Convex/Box/hkpBoxShape.h>
#include <Physics/Utilities/Dynamics/Inertia/hkpInertiaTensorComputer.h>

#include <Physics/Collide/Shape/Convex/Sphere/hkpSphereShape.h>
#include <Physics/Collide/Shape/Convex/Capsule/hkpCapsuleShape.h>

#include <Physics\Dynamics\Constraint\Bilateral\Ragdoll\hkpRagdollConstraintData.h>
#include <Physics\Dynamics\Constraint\Bilateral\BallAndSocket\hkpBallAndSocketConstraintData.h>
#include <Physics\Dynamics\Constraint\Bilateral\Hinge\hkpHingeConstraintData.h>
#include <Physics\Dynamics\Constraint\Bilateral\LimitedHinge\hkpLimitedHingeConstraintData.h>
#include <Physics\Dynamics\Constraint\Bilateral\Prismatic\hkpPrismaticConstraintData.h>
#include <Physics\Dynamics\Constraint\Malleable\hkpMalleableConstraintData.h>
#include <Physics\Dynamics\Constraint\Bilateral\StiffSpring\hkpStiffSpringConstraintData.h>

#include <Animation/Ragdoll/Instance/hkaRagdollInstance.h>
#include <Physics\Dynamics\World\hkpPhysicsSystem.h>
#include <Physics\Utilities\Serialize\hkpPhysicsData.h>

#include <Physics\Collide\Shape\Misc\Transform\hkpTransformShape.h>
#include <Physics\Collide\Shape\Compound\Collection\List\hkpListShape.h>
#include <Physics\Collide\Shape\Deprecated\ConvexList\hkpConvexListShape.h>
#include "Physics/Collide/Shape/Compound/Tree/Mopp/hkpMoppBvTreeShape.h"
#include <Physics\Collide\Shape\Compound\Collection\CompressedMesh\hkpCompressedMeshShape.h>

#include <algorithm>

#include <VHACD.h>
#include <boundingmesh.h>

#include <commands/fixsse.h>

using namespace ckcmd::FBX;
using namespace  ckcmd::Geometry;
using namespace ckcmd::nifscan;
using namespace ckcmd::HKX;

static inline Niflib::Vector3 TOVECTOR3(const hkVector4& v, const float scale = 1.0f) {
	return Niflib::Vector3((float)v.getSimdAt(0) * scale, (float)v.getSimdAt(1) * scale, (float)v.getSimdAt(2) * scale);
}

static inline Niflib::Vector4 TOVECTOR4(const hkVector4& v, const float scale = 1.0f) {
	return Niflib::Vector4((float)v.getSimdAt(0) * scale, (float)v.getSimdAt(1) * scale, (float)v.getSimdAt(2) * scale, (float)v.getSimdAt(3));
}

static inline hkVector4 TOVECTOR4(const Niflib::Vector4& v) {
	return hkVector4(v.x, v.y, v.z, v.w);
}

static inline Niflib::Quaternion TOQUAT(const ::hkQuaternion& q, bool inverse = false) {
	Niflib::Quaternion qt(q.m_vec.getSimdAt(3), q.m_vec.getSimdAt(0), q.m_vec.getSimdAt(1), q.m_vec.getSimdAt(2));
	return inverse ? qt.Inverse() : qt;
}

static inline ::hkQuaternion TOQUAT(const Niflib::Quaternion& q, bool inverse = false) {
	hkVector4 v(q.x, q.y, q.z, q.w);
	v.normalize4();
	::hkQuaternion qt(v.getSimdAt(0), v.getSimdAt(1), v.getSimdAt(2), v.getSimdAt(3));
	if (inverse) qt.setInverse(qt);
	return qt;
}

static inline ::hkQuaternion TOQUAT(const Niflib::hkQuaternion& q, bool inverse = false) {
	hkVector4 v(q.x, q.y, q.z, q.w);
	v.normalize4();
	::hkQuaternion qt(v.getSimdAt(0), v.getSimdAt(1), v.getSimdAt(2), v.getSimdAt(3));
	if (inverse) qt.setInverse(qt);
	return qt;
}

static inline hkMatrix3 TOMATRIX3(const Niflib::InertiaMatrix& q, bool inverse = false) {
	hkMatrix3 m3;
	m3.setCols(TOVECTOR4(q.rows[0]), TOVECTOR4(q.rows[1]), TOVECTOR4(q.rows[2]));
	if (inverse) m3.invert(0.001);
}

static inline Vector4 HKMATRIXROW(const hkTransform& q, const unsigned int row) {
	return Vector4(q(row, 0), q(row, 1), q(row, 2), q(row, 3));
}

static inline FbxVector4 TOFBXVECTOR4(const hkVector4& v)
{
	return FbxVector4(v.getSimdAt(0), v.getSimdAt(1), v.getSimdAt(2), v.getSimdAt(3));
}

static inline Matrix44 TOMATRIX44(const hkTransform& q, const float scale = 1.0f, bool inverse = false) {

	hkVector4 c0 = q.getColumn(0);
	hkVector4 c1 = q.getColumn(1);
	hkVector4 c2 = q.getColumn(2);
	hkVector4 c3 = q.getColumn(3);

	return Matrix44(
		c0.getSimdAt(0), c1.getSimdAt(0), c2.getSimdAt(0), (float)c3.getSimdAt(0)*scale,
		c0.getSimdAt(1), c1.getSimdAt(1), c2.getSimdAt(1), (float)c3.getSimdAt(1)*scale,
		c0.getSimdAt(2), c1.getSimdAt(2), c2.getSimdAt(2), (float)c3.getSimdAt(2)*scale,
		c0.getSimdAt(3), c1.getSimdAt(3), c2.getSimdAt(3), c3.getSimdAt(3)
	);
}

FbxNode* setMatTransform(const Matrix44& av, FbxNode* node, double bake_scale = 1.0) {
	Vector3 translation = av.GetTranslation();
	//
	node->LclTranslation.Set(FbxDouble3(translation.x * bake_scale, translation.y * bake_scale, translation.z *bake_scale));

	Quaternion rotation = av.GetRotation().AsQuaternion();
	Quat QuatTest = { rotation.x, rotation.y, rotation.z, rotation.w };
	EulerAngles inAngs = Eul_FromQuat(QuatTest, EulOrdXYZs);
	node->LclRotation.Set(FbxVector4(rad2deg(inAngs.x), rad2deg(inAngs.y), rad2deg(inAngs.z)));
	node->LclScaling.Set(FbxDouble3(av.GetScale(), av.GetScale(), av.GetScale()));
	return node;
}

FbxNode* setMatATransform(const FbxAMatrix& av, FbxNode* node) {
	node->LclTranslation.Set(av.GetT());
	node->LclRotation.Set(av.GetR());
	node->LclScaling.Set(av.GetS());
	return node;
}

FBXWrangler::FBXWrangler() {
	sdkManager = FbxManager::Create();

	FbxIOSettings* ios = FbxIOSettings::Create(sdkManager, IOSROOT);
	sdkManager->SetIOSettings(ios);

	NewScene();
}

FBXWrangler::~FBXWrangler() {
	if (scene)
		CloseScene();

	if (sdkManager)
		sdkManager->Destroy();
}

void FBXWrangler::NewScene() {
	if (scene)
		CloseScene();

	scene = FbxScene::Create(sdkManager, "ckcmd");
	FbxAxisSystem maxSystem(FbxAxisSystem::EUpVector::eZAxis, (FbxAxisSystem::EFrontVector) - 2, FbxAxisSystem::ECoordSystem::eRightHanded);
	scene->GetGlobalSettings().SetAxisSystem(maxSystem);
	scene->GetRootNode()->LclScaling.Set(FbxDouble3(1.0, 1.0, 1.0));
}

void FBXWrangler::CloseScene() {
	if (scene)
		scene->Destroy();
	
	scene = nullptr;
	comName.clear();
}

namespace std {

	template <>
	struct hash<Triangle>
	{
		std::size_t operator()(const Triangle& k) const
		{
			using std::size_t;
			using std::hash;
			using std::string;

			// Compute individual hash values for first,
			// second and third and combine them using XOR
			// and bit shifting:

			return ((hash<unsigned short>()(k.v1)
				^ (hash<unsigned short>()(k.v2) << 1)) >> 1)
				^ (hash<unsigned short>()(k.v3) << 1);
		}
	};

}

template<>
class Accessor<bhkCompressedMeshShapeData> 
{
	hkGeometry& geometry;
	vector<bhkCMSDMaterial>& havok_materials;
public:

	Accessor(bhkCompressedMeshShapeRef cmesh, hkGeometry& geometry, vector<bhkCMSDMaterial>& materials) : geometry(geometry), havok_materials(materials)
	{
		bhkCompressedMeshShapeDataRef data = cmesh->GetData();

		const vector<bhkCMSDChunk>& chunks = data->chunks;
		const vector<Vector4>& bigVerts = data->GetBigVerts();

		bool multipleShapes = (chunks.size() + bigVerts.empty() ? 0 : 1) > 1;

		if (!data->GetBigVerts().empty())
		{
			const vector<bhkCMSDBigTris>& bigTris = data->GetBigTris();

			for (int i = 0; i < bigVerts.size(); i++)
				geometry.m_vertices.pushBack({ bigVerts[i][0], bigVerts[i][1], bigVerts[i][2] });//verts.push_back({ bigVerts[i][0], bigVerts[i][1], bigVerts[i][2] });

			for (int i = 0; i < bigTris.size(); i++) {
				geometry.m_triangles.pushBack(
					{ 
						(int)bigTris[i].triangle1,
						(int)bigTris[i].triangle2,
						(int)bigTris[i].triangle3,
						(int)bigTris[i].material
					}
				);
			}
		}

		int i = 0;
		havok_materials = data->chunkMaterials;
		const vector<bhkCMSDTransform>& transforms = data->chunkTransforms;
		auto n = chunks.size();
		for (const bhkCMSDChunk& chunk : chunks)
		{
			Vector3 chunkOrigin = { chunk.translation[0], chunk.translation[1], chunk.translation[2] };
			int numOffsets = chunk.numVertices;
			int numIndices = chunk.numIndices;
			int numStrips = chunk.numStrips;
			const vector<unsigned short>& offsets = chunk.vertices;
			const vector<unsigned short>& indices = chunk.indices;
			const vector<unsigned short>& strips = chunk.strips;

			int offset = 0;

			const bhkCMSDTransform& transform = transforms[chunk.transformIndex];

			hkQTransform t;
			t.setTranslation(TOVECTOR4(transform.translation));
			t.setRotation(TOQUAT(transform.rotation));
			hkMatrix4 rr; rr.set(t);


			int verOffsets = geometry.m_vertices.getSize();
			int n = 0;
			for (n = 0; n < (numOffsets / 3); n++) {
				Vector3 vec = chunkOrigin + Vector3(offsets[3 * n], offsets[3 * n + 1], offsets[3 * n + 2]) / 1000.0f;
				hkVector4 p = TOVECTOR4(vec);
				rr.transformPosition(p, p);
				geometry.m_vertices.pushBack(p);
			}

			for (auto s = 0; s < numStrips; s++) {
				for (auto f = 0; f < strips[s] - 2; f++) {
					if ((f + 1) % 2 == 0) {
						geometry.m_triangles.pushBack(
							{ 
								(int)indices[offset + f + 2] + verOffsets, 
								(int)indices[offset + f + 1] + verOffsets, 
								(int)indices[offset + f + 0] + verOffsets, 
								(int)chunk.materialIndex
							}
						);
					}
					else {
						geometry.m_triangles.pushBack(
							{
								(int)indices[offset + f + 0] + verOffsets,
								(int)indices[offset + f + 1] + verOffsets,
								(int)indices[offset + f + 2] + verOffsets,
								(int)chunk.materialIndex
							}
						);
					}
				}
				offset += strips[s];
			}

			// Non-stripped tris
			for (auto f = 0; f < (numIndices - offset); f += 3) {
				geometry.m_triangles.pushBack(
					{
						(int)indices[offset + f + 0] + verOffsets,
						(int)indices[offset + f + 1] + verOffsets,
						(int)indices[offset + f + 2] + verOffsets,
						(int)chunk.materialIndex
					}
				);
			}
		}
	}
};

//class ConstraintVisitor {
//protected:
//	vector<bhkBlendCollisionObjectRef>& nifBodies;
//	hkArray<hkpRigidBody*>& hkBodies;
//
//	hkpRigidBody* getEntity(Ref<bhkEntity> e) {
//		int index = find_if(nifBodies.begin(), nifBodies.end(), [e](bhkBlendCollisionObjectRef b) -> bool { return &(*b->GetBody()) == &*e; }) - nifBodies.begin();
//		if (index < 0 || index >= hkBodies.getSize()) throw runtime_error("Invalid entity into constraint!");
//		return hkBodies[index];
//	}
//public:
//	virtual hkpConstraintData* visit(RagdollDescriptor& constraint) = 0;
//	virtual hkpConstraintData* visit(PrismaticDescriptor& constraint) = 0;
//	virtual hkpConstraintData* visit(MalleableDescriptor& constraint) = 0;
//	virtual hkpConstraintData* visit(HingeDescriptor& constraint) = 0;
//	virtual hkpConstraintData* visit(LimitedHingeDescriptor& constraint) = 0;
//	virtual hkpConstraintData* visit(BallAndSocketDescriptor& constraint) = 0;
//	virtual hkpConstraintData* visit(StiffSpringDescriptor& constraint) = 0;
//
//	virtual hkpConstraintInstance* visitConstraint(bhkConstraintRef constraint) {
//		hkpConstraintData* data = NULL;
//		if (constraint)
//		{
//			if (constraint->IsSameType(bhkRagdollConstraint::TYPE))
//				data = visit(DynamicCast<bhkRagdollConstraint>(constraint)->GetRagdoll());
//			else if (constraint->IsSameType(bhkPrismaticConstraint::TYPE))
//				data = visit(DynamicCast<bhkPrismaticConstraint>(constraint)->GetPrismatic());
//			else if (constraint->IsSameType(bhkMalleableConstraint::TYPE))
//				data = visit(DynamicCast<bhkMalleableConstraint>(constraint)->GetMalleable());
//			else if (constraint->IsSameType(bhkHingeConstraint::TYPE))
//				data = visit(DynamicCast<bhkHingeConstraint>(constraint)->GetHinge());
//			else if (constraint->IsSameType(bhkLimitedHingeConstraint::TYPE))
//				data = visit(DynamicCast<bhkLimitedHingeConstraint>(constraint)->GetLimitedHinge());
//			else if (constraint->IsSameType(bhkBallAndSocketConstraint::TYPE))
//				data = visit(DynamicCast<bhkBallAndSocketConstraint>(constraint)->GetBallAndSocket());
//			else if (constraint->IsSameType(bhkStiffSpringConstraint::TYPE))
//				data = visit(DynamicCast<bhkStiffSpringConstraint>(constraint)->GetStiffSpring());
//			else
//				throw new runtime_error("Unimplemented constraint type!");
//			return new hkpConstraintInstance(getEntity(constraint->GetEntities()[0]), getEntity(constraint->GetEntities()[1]), data);
//		}
//	}
//
//	ConstraintVisitor(vector<bhkBlendCollisionObjectRef>& nbodies, hkArray<hkpRigidBody*>& hkbodies) : nifBodies(nbodies), hkBodies(hkbodies) {}
//};
//
//class FBXConstraintBuilder : public ConstraintVisitor {
//public:
//
//	virtual hkpConstraintData* visit(BallAndSocketDescriptor& constraint)
//	{
//		hkpBallAndSocketConstraintData* data = new hkpBallAndSocketConstraintData();
//		return data;
//	}
//
//	virtual hkpConstraintData* visit(StiffSpringDescriptor& constraint)
//	{
//		hkpStiffSpringConstraintData* data = new hkpStiffSpringConstraintData();
//		return data;
//	}
//
//	virtual hkpConstraintData* visit(RagdollDescriptor& descriptor) {
//		hkpRagdollConstraintData* data = new hkpRagdollConstraintData();
//		data->setInBodySpace(
//			TOVECTOR4(descriptor.pivotA * 7),
//			TOVECTOR4(descriptor.pivotB * 7),
//			TOVECTOR4(descriptor.planeA),
//			TOVECTOR4(descriptor.planeB),
//			TOVECTOR4(descriptor.twistA),
//			TOVECTOR4(descriptor.twistB)
//		);
//		return data;
//	}
//
//	virtual hkpConstraintData* visit(PrismaticDescriptor& descriptor) {
//		//hkpPrismaticConstraintData* data = new hkpPrismaticConstraintData();
//		//data->setInBodySpace(
//		//	TOVECTOR4(descriptor.pivotA * 7),
//		//	TOVECTOR4(descriptor.pivotB * 7),
//		//	);
//		//);
//
//		//return new hkpConstraintInstance(getEntity(constraint.GetEntities()[0]), getEntity(constraint.GetEntities()[1]), data);
//		return NULL;
//	}
//
//	virtual hkpConstraintData* visit(MalleableDescriptor& descriptor) {
//		switch (descriptor.type) {
//			case BALLANDSOCKET:
//				return visit(descriptor.ballAndSocket);
//			case HINGE:
//				return visit(descriptor.hinge);
//			case LIMITED_HINGE:
//				return visit(descriptor.limitedHinge);
//			case PRISMATIC:
//				return visit(descriptor.prismatic);
//			case RAGDOLL:
//				return visit(descriptor.ragdoll);
//			case STIFFSPRING:
//				return visit(descriptor.stiffSpring);
//			case MALLEABLE:
//				break;
//		}
//		
//		return NULL;
//	}
//
//	virtual hkpConstraintData* visit(HingeDescriptor& descriptor) {
//		hkpHingeConstraintData* data = new hkpHingeConstraintData();
//		data->setInBodySpace(
//			TOVECTOR4(descriptor.pivotA * 7),
//			TOVECTOR4(descriptor.pivotB * 7),
//			TOVECTOR4(descriptor.axleA),
//			TOVECTOR4(descriptor.axleB)
//		);
//		return data;
//	}
//	virtual hkpConstraintData* visit(LimitedHingeDescriptor& descriptor){
//		hkpLimitedHingeConstraintData* data = new hkpLimitedHingeConstraintData();
//		data->setInBodySpace(
//			TOVECTOR4(descriptor.pivotA * 7),
//			TOVECTOR4(descriptor.pivotB * 7),
//			TOVECTOR4(descriptor.axleA),
//			TOVECTOR4(descriptor.axleB),
//			TOVECTOR4(descriptor.perp2AxleInA1),
//			TOVECTOR4(descriptor.perp2AxleInB1)
//		);
//		return data;
//	}
//
//	ConstraintBuilder(vector<bhkBlendCollisionObjectRef>& nbodies, hkArray<hkpRigidBody*>& hkbodies) : ConstraintVisitor(nbodies,hkbodies) {}
//};

class FBXBuilderVisitor : public RecursiveFieldVisitor<FBXBuilderVisitor> {
	const NifInfo& this_info;
	const string& texturePath;
	FbxScene& scene;
	deque<FbxNode*> build_stack;
	set<void*>& alreadyVisitedNodes;
	map<NiSkinInstance*, NiTriBasedGeom*> skins;
	map<bhkRigidBodyRef, FbxNode*> bodies;
	set<NiControllerManager*>& managers;
	NifFile& nif_file;
	FbxAnimStack* lAnimStack = NULL;
	string root_name;
	double bhkScaleFactor = 1.0;

	FbxFileTexture* create_texture(const char* texture_type, const string& texture_path, const FbxFileTexture::ETextureUse use = FbxTexture::eStandard)
	{
		FbxFileTexture* lTexture = FbxFileTexture::Create(&scene, texture_type);
		// Set texture properties.
		fs::path out_path = fs::path(texturePath) / texture_path;
		lTexture->SetFileName(out_path.string().c_str()); // Resource file is in current directory.		
		lTexture->SetTextureUse(use);
		lTexture->SetMappingType(FbxTexture::eUV);
		lTexture->SetMaterialUse(FbxFileTexture::eModelMaterial);
		lTexture->SetSwapUV(false);
		lTexture->SetTranslation(0.0, 0.0);
		lTexture->SetScale(1.0, 1.0);
		lTexture->SetRotation(0.0, 0.0);
		return lTexture;
	}


	union alpha_flags_modes
	{
		struct {
			// Bit 0 : alpha blending enable
			unsigned alpha_blending_enable : 1;
			// Bits 1-4 : source blend mode
			unsigned source_blend_mode : 4;
			// Bits 5-8 : destination blend mode
			unsigned destination_blend_mode : 4;
			// Bit 9 : alpha test enable
			unsigned alpha_test_enable : 1;
			// Bit 10-12 : alpha test mode
			unsigned alpha_test_mode : 3;
			// Bit 13 : no sorter flag ( disables triangle sorting )
			unsigned no_sorter_flag : 1;
		} bits;
		unsigned int value;	
	};

	enum gl_blend_modes {
		GL_ONE = 0,
		GL_ZERO = 1,
		GL_SRC_COLOR = 2,
		GL_ONE_MINUS_SRC_COLOR = 3,
		GL_DST_COLOR = 4,
		GL_ONE_MINUS_DST_COLOR = 5,
		GL_SRC_ALPHA = 6,
		GL_ONE_MINUS_SRC_ALPHA = 7,
		GL_DST_ALPHA = 8,
		GL_ONE_MINUS_DST_ALPHA = 9,
		GL_SRC_ALPHA_SATURATE = 10
	};

	FbxSurfaceMaterial* create_material(const string& name, BSLightingShaderPropertyRef material_property, NiAlphaPropertyRef alpha)
	{
		if (material_property != NULL)
		{
			alreadyVisitedNodes.insert(material_property);

			string lShadingName = "Phong";
			string m_name = name + "_material";
			sanitizeString(m_name);
			FbxDouble3 lDiffuse(1.0, 1.0, 1.0);
			FbxDouble3 lBlack(0.0, 0.0, 0.0);
			FbxSurfacePhong* gMaterial = FbxSurfacePhong::Create(scene.GetFbxManager(), m_name.c_str());
			//The following properties are used by the Phong shader to calculate the color for each pixel in the material :

			// Generate primary and secondary colors.
			Color3 nif_emissive_color = material_property->GetEmissiveColor();
			gMaterial->Emissive = { nif_emissive_color.r, nif_emissive_color.g, nif_emissive_color.b };
			gMaterial->EmissiveFactor = material_property->GetEmissiveMultiple();

			
			//Specular
			Color3 nif_specular_color = material_property->GetSpecularColor();
			gMaterial->Specular = { nif_specular_color.r, nif_specular_color.g, nif_specular_color.b };
			FbxDouble3 scolor = gMaterial->Specular.Get();
			
			//gMaterial->SpecularFactor.Set(material_property->GetSpecularStrength());

			//Diffuse
			gMaterial->Diffuse = lDiffuse;
			//gMaterial->DiffuseFactor = material_property->GetSpecularStrength();

			//Ambient
			gMaterial->Ambient = lBlack;
			gMaterial->AmbientFactor.Set(1.);

			gMaterial->Shininess = material_property->GetGlossiness();
			gMaterial->ShadingModel.Set(lShadingName.c_str());

			if (material_property->GetTextureSet() != NULL)
			{
				alreadyVisitedNodes.insert(material_property->GetTextureSet());
				vector<string>& texture_set = material_property->GetTextureSet()->GetTextures();
				//TODO: support more 
				if (!texture_set.empty())
				{
					if (!texture_set[0].empty())
					{
						FbxFileTexture* diffuse = create_texture(FbxSurfaceMaterial::sDiffuse, texture_set[0]);
						diffuse->SetTranslation(material_property->GetUvOffset().u, material_property->GetUvOffset().v);
						diffuse->SetScale(material_property->GetUvScale().u, material_property->GetUvScale().v);
						switch (material_property->GetTextureClampMode())
						{
						case CLAMP_S_CLAMP_T: 
							/*!< Clamp in both directions. */
							diffuse->SetWrapMode(FbxTexture::EWrapMode::eClamp, FbxTexture::EWrapMode::eClamp);
							break;
						case CLAMP_S_WRAP_T:
							/*!< Clamp in the S(U) direction but wrap in the T(V) direction. */
							diffuse->SetWrapMode(FbxTexture::EWrapMode::eClamp, FbxTexture::EWrapMode::eRepeat);
							break;
						case WRAP_S_CLAMP_T:
							/*!< Wrap in the S(U) direction but clamp in the T(V) direction. */
							diffuse->SetWrapMode(FbxTexture::EWrapMode::eRepeat, FbxTexture::EWrapMode::eClamp);
							break;
						case WRAP_S_WRAP_T:
							/*!< Wrap in both directions. */
							diffuse->SetWrapMode(FbxTexture::EWrapMode::eRepeat, FbxTexture::EWrapMode::eRepeat);
							break;
						default:
							break;
						};

						diffuse->Alpha = material_property->GetAlpha();

						if (diffuse && gMaterial)
						{
							gMaterial->Diffuse.ConnectSrcObject(diffuse);
					//		if (alpha != NULL)
					//		{
					//			alreadyVisitedNodes.insert(alpha);
					//			alpha_flags_modes alpha_flags;
					//			alpha_flags.value = alpha->GetFlags();

					//			if (alpha_flags.bits.alpha_blending_enable)
					//			{
					//				diffuse->SetBlendMode(FbxTexture::EBlendMode::eTranslucent);
					//				diffuse->SetAlphaSource(FbxTexture::EAlphaSource::eRGBIntensity);

					//			}
					//			if (alpha_flags.bits.source_blend_mode == gl_blend_modes::GL_ONE && alpha_flags.bits.destination_blend_mode == gl_blend_modes::GL_ONE)
					//			{
					//				diffuse->SetBlendMode(FbxTexture::EBlendMode::eAdditive);
					//			}
					//			if (alpha_flags.bits.source_blend_mode == gl_blend_modes::GL_ZERO && alpha_flags.bits.destination_blend_mode == gl_blend_modes::GL_SRC_COLOR)
					//			{
					//				diffuse->SetBlendMode(FbxTexture::EBlendMode::eModulate);
					//			}
					//			if (alpha_flags.bits.source_blend_mode == gl_blend_modes::GL_DST_COLOR && alpha_flags.bits.destination_blend_mode == gl_blend_modes::GL_SRC_COLOR)
					//			{
					//				diffuse->SetBlendMode(FbxTexture::EBlendMode::eModulate2);
					//			}
					//			if (alpha_flags.bits.alpha_test_enable)
					//			{
					//				diffuse->SetBlendMode(FbxTexture::EBlendMode::eTranslucent);
					//				diffuse->SetAlphaSource(FbxTexture::EAlphaSource::eBlack);
					//			}

					//			/*

					//			// Bit 0 : alpha blending enable
					//			// Bits 1-4 : source blend mode
					//			// Bits 5-8 : destination blend mode
					//			// Bit 9 : alpha test enable
					//			// Bit 10-12 : alpha test mode
					//			// Bit 13 : no sorter flag ( disables triangle sorting )
					//			//
					//			// blend modes (glBlendFunc):
					//			// 0000 GL_ONE
					//			//             0001 GL_ZERO
					//			//             0010 GL_SRC_COLOR
					//			//             0011 GL_ONE_MINUS_SRC_COLOR
					//			//             0100 GL_DST_COLOR
					//			//             0101 GL_ONE_MINUS_DST_COLOR
					//			//             0110 GL_SRC_ALPHA
					//			//             0111 GL_ONE_MINUS_SRC_ALPHA
					//			//             1000 GL_DST_ALPHA
					//			//             1001 GL_ONE_MINUS_DST_ALPHA
					//			//             1010 GL_SRC_ALPHA_SATURATE

					//			For Alpha Blending
					//				Source Blend Mode: Src Alpha
					//				Destination Blend Mode: Inv Src Alpha
					//			For Additive Blending
					//				Source Blend Mode: One
					//				Destination Blend Mode: One
					//			For Multiplicative Blending
					//				Source Blend Mode: Zero
					//				Destination Blend Mode: Src Color
					//			For 2x Multiplicative Blending
					//				Source Blend Mode: Dst Color
					//				Destination Blend Mode: Src Color


					//			For Alpha Testing check Enable Testing
					//			Alpha Test Function sets how transparency channel grey values (0 to 255 or black to white) will be compared to the Alpha Test Threshold value to determine what is opaque
					//				Less or Equal: Lighter will be more transparent
					//				Greater or Equal: Darker will be more transparent
					//			Alpha Test Threshold: Value from 0 to 255 (black to white)
					//			*/

					//		}
						}
					}
					if (!texture_set[1].empty())
					{
						FbxFileTexture* normal = create_texture(FbxSurfaceMaterial::sNormalMap, texture_set[1] );
						if (normal && gMaterial)
							gMaterial->NormalMap.ConnectSrcObject(normal);
					}
				}
			}



			return gMaterial;
		}
		return NULL;
	}

	template<typename shape_type, const unsigned int>
	FbxSurfaceMaterial* extract_Material(shape_type& shape)
	{
		return NULL;
	}

	template<>
	FbxSurfaceMaterial* extract_Material<NiTriStrips, VER_20_2_0_7>(NiTriStrips& shape)
	{
		return create_material(shape.GetName(), DynamicCast<BSLightingShaderProperty>(shape.GetShaderProperty()), shape.GetAlphaProperty());
	}

	template<>
	FbxSurfaceMaterial* extract_Material<NiTriShape, VER_20_2_0_7>(NiTriShape& shape)
	{
		return create_material(shape.GetName(), DynamicCast<BSLightingShaderProperty>(shape.GetShaderProperty()), shape.GetAlphaProperty());
	}

	FbxNode* AddGeometry(NiTriStrips& node) {
		string shapeName = node.GetName();
		sanitizeString(shapeName);

		if (node.GetData() == NULL) return FbxNode::Create(&scene, shapeName.c_str());

		const vector<Vector3>& verts = node.GetData()->GetVertices();
		const vector<Vector3>& norms = node.GetData()->GetNormals();

		vector<Triangle>& tris = vector<Triangle>(0);
		vector<TexCoord>& uvs = vector<TexCoord>(0);
		vector<Color4>& vcs = vector<Color4>(0);

		if (node.GetData()->IsSameType(NiTriStripsData::TYPE)) {
			NiTriStripsDataRef ref = DynamicCast<NiTriStripsData>(node.GetData());
			tris = triangulate(ref->GetPoints());
			if (!ref->GetUvSets().empty())
				uvs = ref->GetUvSets()[0];
			if (!ref->GetVertexColors().empty())
				vcs = ref->GetVertexColors();
		}

		alreadyVisitedNodes.insert(node.GetData());

		//defer skin
		if (node.GetSkinInstance() != NULL)
		{
			alreadyVisitedNodes.insert(node.GetSkinInstance());
			if (node.GetSkinInstance()->GetData() != NULL)
				alreadyVisitedNodes.insert(node.GetSkinInstance()->GetData());
			if (node.GetSkinInstance()->GetSkinPartition() != NULL)
				alreadyVisitedNodes.insert(node.GetSkinInstance()->GetSkinPartition());
		}


		FbxSurfaceMaterial* material = extract_Material<NiTriStrips, VER_20_2_0_7>(node);

		return AddGeometry(shapeName, verts, norms, tris, uvs, vcs, material);
	}

	FbxNode* AddGeometry(NiTriShape& node) {

		string shapeName = node.GetName();
		sanitizeString(shapeName);

		if (node.GetData() == NULL) return FbxNode::Create(&scene, shapeName.c_str());

		const vector<Vector3>& verts = node.GetData()->GetVertices();
		const vector<Vector3>& norms = node.GetData()->GetNormals();

		vector<Triangle>& tris = vector<Triangle>(0);
		vector<TexCoord>& uvs = vector<TexCoord>(0);
		vector<Color4>& vcs = vector<Color4>(0);

		if (node.GetData()->IsSameType(NiTriShapeData::TYPE)) {
			NiTriShapeDataRef ref = DynamicCast<NiTriShapeData>(node.GetData());
			tris = ref->GetTriangles();
			if (!ref->GetUvSets().empty())
				uvs = ref->GetUvSets()[0];
			if (!ref->GetVertexColors().empty())
				vcs = ref->GetVertexColors();
		}

		alreadyVisitedNodes.insert(node.GetData());

		//defer skin
		if (node.GetSkinInstance() != NULL)
		{
			alreadyVisitedNodes.insert(node.GetSkinInstance());
			if (node.GetSkinInstance()->GetData() != NULL)
				alreadyVisitedNodes.insert(node.GetSkinInstance()->GetData());
			if (node.GetSkinInstance()->GetSkinPartition() != NULL)
				alreadyVisitedNodes.insert(node.GetSkinInstance()->GetSkinPartition());
		}

		if (verts.empty())
			return FbxNode::Create(&scene, shapeName.c_str());

		FbxSurfaceMaterial* material = extract_Material<NiTriShape, VER_20_2_0_7>(node);

		return AddGeometry(shapeName, verts, norms, tris, uvs, vcs, material);
	}

	FbxNode* AddGeometry(const string& shapeName, 
							const vector<Vector3>& verts,
							const vector<Vector3>& norms,
							const vector<Triangle>& tris,
							vector<TexCoord>& uvs, 
							vector<Color4>& vcs, 
							FbxSurfaceMaterial* material) {

		FbxMesh* m = FbxMesh::Create(&scene, shapeName.c_str());

		FbxGeometryElementNormal* normElement = nullptr;
		if (!norms.empty()) {
			normElement = m->CreateElementNormal();
			normElement->SetMappingMode(FbxLayerElement::eByControlPoint);
			normElement->SetReferenceMode(FbxLayerElement::eDirect);
		}

		FbxGeometryElementUV* uvElement = nullptr;
		if (!uvs.empty()) {
			std::string uvName = shapeName + "UV";
			uvElement = m->CreateElementUV(uvName.c_str());
			uvElement->SetMappingMode(FbxGeometryElement::eByControlPoint);
			uvElement->SetReferenceMode(FbxGeometryElement::eDirect);
		}

		FbxGeometryElementVertexColor* vcElement = nullptr;
		if (!vcs.empty()) {
			vcElement = m->CreateElementVertexColor();
			vcElement->SetMappingMode(FbxGeometryElement::eByControlPoint);
			vcElement->SetReferenceMode(FbxGeometryElement::eDirect);
		}

		m->InitControlPoints(verts.size());
		FbxVector4* points = m->GetControlPoints();

		for (int i = 0; i < m->GetControlPointsCount(); i++) {
			points[i] = FbxVector4(verts[i].x, verts[i].y, verts[i].z);
			if (normElement)
				normElement->GetDirectArray().Add(FbxVector4(norms[i].x, norms[i].y, norms[i].z));
			if (uvElement)
				uvElement->GetDirectArray().Add(FbxVector2(uvs[i].u, 1 - uvs[i].v));
			if (vcElement)
				vcElement->GetDirectArray().Add(FbxColor(vcs[i].r, vcs[i].g, vcs[i].b, vcs[i].a));
		}

		if (!tris.empty()) {
			for (auto &t : tris) {
				if (material != NULL)
				{
					m->BeginPolygon(0);
				}
				else {
					m->BeginPolygon(1);
				}
				m->AddPolygon(t.v1);
				m->AddPolygon(t.v2);
				m->AddPolygon(t.v3);
				m->EndPolygon();
			}
		}

		FbxNode* mNode = FbxNode::Create(&scene, shapeName.c_str());
		mNode->SetNodeAttribute(m);

		//handle material
		if (material != NULL)
			mNode->AddMaterial(material);


		return mNode;
	}

	FbxNode* setNullTransform(FbxNode* node) {
		node->LclTranslation.Set(FbxDouble3(0.0,0.0,0.0));
		node->LclRotation.Set(FbxVector4(0.0,0.0,0.0));
		node->LclScaling.Set(FbxDouble3(1.0,1.0,1.0));

		return node;
	}

	FbxNode* setTransform(NiAVObject* av, FbxNode* node) {
		Vector3 translation = av->GetTranslation();
		//
		node->LclTranslation.Set(FbxDouble3(translation.x, translation.y, translation.z));

		Quaternion rotation = av->GetRotation().AsQuaternion();
		Quat QuatTest = { rotation.x, rotation.y, rotation.z, rotation.w };
		EulerAngles inAngs = Eul_FromQuat(QuatTest, EulOrdXYZs);
		node->LclRotation.Set(FbxVector4(rad2deg(inAngs.x), rad2deg(inAngs.y), rad2deg(inAngs.z)));
		node->LclScaling.Set(FbxDouble3(av->GetScale(), av->GetScale(), av->GetScale()));

		return node;
	}

	template<typename T>
	FbxNode* getBuiltNode(T& obj) {
		NiObject* node = (NiObject*)&obj;
		if (node->IsDerivedType(NiAVObject::TYPE))
			return getBuiltNode(string(DynamicCast<NiAVObject>(node)->GetName().c_str()));
		return NULL;
	}

	template<typename T>
	FbxNode* getBuiltNode(Ref<T>& obj) {
		NiObjectRef node = (NiObjectRef)obj;
		if (node->IsDerivedType(NiAVObject::TYPE))
			return getBuiltNode(string(DynamicCast<NiAVObject>(node)->GetName().c_str()));
		return NULL;
	}


	template<>
	inline FbxNode* getBuiltNode(NiNode*& node) {
		return getBuiltNode(string(node->GetName().c_str()));
	}

	template<>
	inline FbxNode* getBuiltNode(string& name) {
		string sanitized = name; sanitizeString(sanitized);
		if (name == root_name || sanitized == root_name)
			return scene.GetRootNode();
		FbxNode* result = scene.FindNodeByName(name.c_str());
		if (result == NULL)
			result = scene.FindNodeByName(sanitized.c_str());
		return result;
	}

	template<>
	inline FbxNode* getBuiltNode(const string& name) {
		string sanitized = name; sanitizeString(sanitized);
		if (name == root_name || sanitized == root_name)
			return scene.GetRootNode();
		FbxNode* result = scene.FindNodeByName(name.c_str());
		if (result == NULL)
			result = scene.FindNodeByName(sanitized.c_str());
		return result;
	}

	void processSkins() {
		if (skins.empty())
			return;
		for (pair<NiSkinInstance*, NiTriBasedGeom*> skin : skins) {
			NiTriBasedGeomRef mesh = skin.second;

			NiSkinDataRef data = skin.first->GetData();
			NiSkinPartitionRef part = skin.first->GetSkinPartition();
			vector<BoneData>& bonelistdata = data->GetBoneList();
			std::string shapeSkin = mesh->GetName() + "_skin";


			vector<NiNode * > data_bonelist = skin.first->GetBones();

			int partIndex = 0;
			for (const auto& part_data : part->GetSkinPartitionBlocks())
			{
				FbxSkin* fbx_skin = FbxSkin::Create(&scene, shapeSkin.c_str());
				map<unsigned short, FbxCluster*> clusters;
				//create clusters
				for (unsigned short bone_index : part_data.bones) {
					NiNode* bone = skin.first->GetBones()[bone_index];
					FbxNode* jointNode = getBuiltNode(bone);
					if (jointNode) {
						std::string boneSkin = bone->GetName() + "_cluster";
						FbxCluster* aCluster = FbxCluster::Create(&scene, boneSkin.c_str());
						aCluster->SetLink(jointNode);
						aCluster->SetLinkMode(FbxCluster::eTotalOne);

						Vector3 translation = data->GetSkinTransform().translation;
						Quaternion rotation = data->GetSkinTransform().rotation.AsQuaternion();
						float scale = data->GetSkinTransform().scale;

						FbxAMatrix global_transform;
						global_transform.SetT(FbxDouble3(translation.x, translation.y, translation.z));
						global_transform.SetQ({ rotation.x, rotation.y, rotation.z, rotation.w });
						global_transform.SetS(FbxDouble3(scale, scale, scale));

						FbxAMatrix joint_transform = jointNode->EvaluateGlobalTransform();

						aCluster->SetTransformLinkMatrix(joint_transform);
						aCluster->SetTransformMatrix(global_transform.Inverse());

						clusters[bone_index] = aCluster;
					}
				}

				for (int v = 0; v < part_data.vertexMap.size(); v++)
				{
					for (int b = 0; b < part_data.boneIndices[v].size(); b++)
					{
						unsigned short bone_index = part_data.bones[part_data.boneIndices[v][b]];
						FbxCluster* cluster = clusters[bone_index];
						unsigned short vertex_index = part_data.vertexMap[v];
						float vertex_weight = part_data.vertexWeights[v][b];
						cluster->AddControlPointIndex(vertex_index, vertex_weight);
					}
				}

				for (const auto& cluster : clusters)
				{
					fbx_skin->AddCluster(cluster.second);
				}

				FbxMesh* shapeMesh = (FbxMesh*)getBuiltNode(mesh)->GetNodeAttribute();
				if (shapeMesh)
					shapeMesh->AddDeformer(fbx_skin);
			}
		}
	}

	string getPalettedString(NiStringPaletteRef nipalette, unsigned int offset) {
		StringPalette s_palette = nipalette->GetPalette();
		const char* c_palette = s_palette.palette.c_str();
		for (size_t i = offset; i < s_palette.length; i++)
		{
			if (c_palette[i] == 0)
				return string(&c_palette[offset], &c_palette[i]);
		}
		return "";
	}

	template<typename interpolatorT>
	void addTrack(interpolatorT& interpolator, FbxNode* node, FbxAnimLayer *lAnimLayer) {
		//		throw runtime_error("addTrack: Unsupported interpolator type!");
	}


	// For reference
	//enum KeyType {
	//	LINEAR_KEY = 1, /*!< Use linear interpolation. */
	//	QUADRATIC_KEY = 2, /*!< Use quadratic interpolation.  Forward and back tangents will be stored. */
	//	TBC_KEY = 3, /*!< Use Tension Bias Continuity interpolation.  Tension, bias, and continuity will be stored. */
	//	XYZ_ROTATION_KEY = 4, /*!< For use only with rotation data.  Separate X, Y, and Z keys will be stored instead of using quaternions. */
	//	CONST_KEY = 5, /*!< Step function. Used for visibility keys in NiBoolData. */
	//};


	template<typename T>
	bool getFieldIsValid(T* object, unsigned int field) {
		vector<unsigned int> valid_translations_fields = object->GetValidFieldsIndices(this_info);
		return find(valid_translations_fields.begin(), valid_translations_fields.end(), field)
			!= valid_translations_fields.end();
	}

	void addKeys(KeyGroup<Vector3>& translations, FbxNode* node, FbxAnimLayer *lAnimLayer) {
		if (translations.numKeys > 0) {
			FbxAnimCurve* lCurve_Trans_X = node->LclTranslation.GetCurve(lAnimLayer, FBXSDK_CURVENODE_COMPONENT_X, false);
			if (lCurve_Trans_X == NULL)
				lCurve_Trans_X = node->LclTranslation.GetCurve(lAnimLayer, FBXSDK_CURVENODE_COMPONENT_X, true);
			FbxAnimCurve* lCurve_Trans_Y = node->LclTranslation.GetCurve(lAnimLayer, FBXSDK_CURVENODE_COMPONENT_Y, false);
			if (lCurve_Trans_Y == NULL)
				lCurve_Trans_Y = node->LclTranslation.GetCurve(lAnimLayer, FBXSDK_CURVENODE_COMPONENT_Y, true);
			FbxAnimCurve* lCurve_Trans_Z = node->LclTranslation.GetCurve(lAnimLayer, FBXSDK_CURVENODE_COMPONENT_Z, false);
			if (lCurve_Trans_Z == NULL)
				lCurve_Trans_Z = node->LclTranslation.GetCurve(lAnimLayer, FBXSDK_CURVENODE_COMPONENT_Z, true);

			FbxAnimCurveDef::EInterpolationType translation_interpolation_type = FbxAnimCurveDef::eInterpolationLinear;

			if (getFieldIsValid(&translations, KeyGroup<Vector3>::FIELDS::interpolation)) {
				switch (translations.interpolation) {
				case CONST_KEY:
					translation_interpolation_type = FbxAnimCurveDef::eInterpolationConstant;
					break;
				case LINEAR_KEY:
					translation_interpolation_type = FbxAnimCurveDef::eInterpolationLinear;
					break;
				case QUADRATIC_KEY:
					translation_interpolation_type = FbxAnimCurveDef::eInterpolationCubic;
					break;
				}
			}

			for (const Key<Vector3>& key : translations.keys) {

				FbxTime lTime;
				//key.time
				lTime.SetTime(0, 0, key.time, 0, 0, 0, lTime.eFrames30);

				lCurve_Trans_X->KeyModifyBegin();
				int lKeyIndex = lCurve_Trans_X->KeyAdd(lTime);
				lCurve_Trans_X->KeySetValue(lKeyIndex, key.data.x);
				lCurve_Trans_X->KeySetInterpolation(lKeyIndex, translation_interpolation_type);
				lCurve_Trans_X->KeyModifyEnd();

				lCurve_Trans_Y->KeyModifyBegin();
				lKeyIndex = lCurve_Trans_Y->KeyAdd(lTime);
				lCurve_Trans_Y->KeySetValue(lKeyIndex, key.data.y);
				lCurve_Trans_Y->KeySetInterpolation(lKeyIndex, translation_interpolation_type);
				lCurve_Trans_Y->KeyModifyEnd();

				lCurve_Trans_Z->KeyModifyBegin();
				lKeyIndex = lCurve_Trans_Z->KeyAdd(lTime);
				lCurve_Trans_Z->KeySetValue(lKeyIndex, key.data.z);
				lCurve_Trans_Z->KeySetInterpolation(lKeyIndex, translation_interpolation_type);
				lCurve_Trans_Z->KeyModifyEnd();
			}
		}
	}
	void addKeys(const Niflib::array<3, KeyGroup<float > >& rotations, FbxNode* node, FbxAnimLayer *lAnimLayer) {
		if (!rotations.empty()) {
			FbxAnimCurve* lCurve_Rot_X = node->LclRotation.GetCurve(lAnimLayer, FBXSDK_CURVENODE_COMPONENT_X, false);
			if (lCurve_Rot_X == NULL)
				lCurve_Rot_X = node->LclRotation.GetCurve(lAnimLayer, FBXSDK_CURVENODE_COMPONENT_X, true);
			FbxAnimCurve* lCurve_Rot_Y = node->LclRotation.GetCurve(lAnimLayer, FBXSDK_CURVENODE_COMPONENT_Y, false);
			if (lCurve_Rot_Y == NULL)
				lCurve_Rot_Y = node->LclRotation.GetCurve(lAnimLayer, FBXSDK_CURVENODE_COMPONENT_Y, true);
			FbxAnimCurve* lCurve_Rot_Z = node->LclRotation.GetCurve(lAnimLayer, FBXSDK_CURVENODE_COMPONENT_Z, false);
			if (lCurve_Rot_Z == NULL)
				lCurve_Rot_Z = node->LclRotation.GetCurve(lAnimLayer, FBXSDK_CURVENODE_COMPONENT_Z, true);

			Niflib::array<3, FbxAnimCurve* > curves;
			curves[0] = lCurve_Rot_X;
			curves[1] = lCurve_Rot_Y;
			curves[2] = lCurve_Rot_Z;

			for (int i = 0; i < 3; i++) {
				for (const Key<float>& key : rotations.at(i).keys) {

					FbxTime lTime;
					//key.time
					lTime.SetTime(0, 0, key.time, 0, 0, 0, lTime.eFrames30);

					curves[i]->KeyModifyBegin();
					int lKeyIndex = curves[i]->KeyAdd(lTime);
					curves[i]->KeySetValue(lKeyIndex, float(rad2deg(key.data)));
					curves[i]->KeySetInterpolation(lKeyIndex, FbxAnimCurveDef::eInterpolationCubic);
					curves[i]->KeySetLeftDerivative(lKeyIndex, key.backward_tangent);
					curves[i]->KeySetRightDerivative(lKeyIndex, key.forward_tangent);
					curves[i]->KeyModifyEnd();
				}
			}
		}
	}
	void addKeys(const vector<Key<Quaternion > >& rotations, FbxNode* node, FbxAnimLayer *lAnimLayer) {
		if (!rotations.empty()) {
			FbxAnimCurve* lCurve_Rot_X = node->LclRotation.GetCurve(lAnimLayer, FBXSDK_CURVENODE_COMPONENT_X, false);
			if (lCurve_Rot_X == NULL)
				lCurve_Rot_X = node->LclRotation.GetCurve(lAnimLayer, FBXSDK_CURVENODE_COMPONENT_X, true);
			FbxAnimCurve* lCurve_Rot_Y = node->LclRotation.GetCurve(lAnimLayer, FBXSDK_CURVENODE_COMPONENT_Y, false);
			if (lCurve_Rot_Y == NULL)
				lCurve_Rot_Y = node->LclRotation.GetCurve(lAnimLayer, FBXSDK_CURVENODE_COMPONENT_Y, true);
			FbxAnimCurve* lCurve_Rot_Z = node->LclRotation.GetCurve(lAnimLayer, FBXSDK_CURVENODE_COMPONENT_Z, false);
			if (lCurve_Rot_Z == NULL)
				lCurve_Rot_Z = node->LclRotation.GetCurve(lAnimLayer, FBXSDK_CURVENODE_COMPONENT_Z, true);

			for (const Key<Quaternion>& key : rotations) {
				FbxTime lTime;
				//key.time
				lTime.SetTime(0, 0, key.time, 0, 0, 0, lTime.eFrames30);

				FbxQuaternion fbx_quat(key.data.x, key.data.y, key.data.z, key.data.w);
				FbxVector4 xyz = fbx_quat.DecomposeSphericalXYZ();

				lCurve_Rot_X->KeyModifyBegin();
				int lKeyIndex = lCurve_Rot_X->KeyAdd(lTime);
				lCurve_Rot_X->KeySetValue(lKeyIndex, xyz[0]);
				lCurve_Rot_X->KeySetInterpolation(lKeyIndex, FbxAnimCurveDef::eInterpolationCubic);
				lCurve_Rot_X->KeyModifyEnd();

				lCurve_Rot_Y->KeyModifyBegin();
				lKeyIndex = lCurve_Rot_Y->KeyAdd(lTime);
				lCurve_Rot_Y->KeySetValue(lKeyIndex, xyz[1]);
				lCurve_Rot_Y->KeySetInterpolation(lKeyIndex, FbxAnimCurveDef::eInterpolationCubic);
				lCurve_Rot_Y->KeyModifyEnd();

				lCurve_Rot_Z->KeyModifyBegin();
				lKeyIndex = lCurve_Rot_Z->KeyAdd(lTime);
				lCurve_Rot_Z->KeySetValue(lKeyIndex, xyz[2]);
				lCurve_Rot_Z->KeySetInterpolation(lKeyIndex, FbxAnimCurveDef::eInterpolationCubic);
				lCurve_Rot_Z->KeyModifyEnd();
			}
		}
	}

	template<>
	void addTrack(NiTransformInterpolator& interpolator, FbxNode* node, FbxAnimLayer *lAnimLayer) {
		//here we have an initial transform to be applied and a set of keys
		FbxAMatrix local = node->EvaluateLocalTransform();
		NiQuatTransform interp_local = interpolator.GetTransform();
		//TODO: check these;
		NiTransformDataRef data = interpolator.GetData();

		if (data != NULL) {

			//translations:
			addKeys(data->GetTranslations(), node, lAnimLayer);

			//rotations:
			if (getFieldIsValid(&*data, NiKeyframeData::FIELDS::rotationType)) {
				if (data->GetRotationType() == XYZ_ROTATION_KEY) {
					//single float groups with tangents
					addKeys(data->GetXyzRotations(), node, lAnimLayer);
				}
				else {
					//threat as quaternion?
					addKeys(data->GetQuaternionKeys(), node, lAnimLayer);
				}
			}
			else {
				addKeys(data->GetQuaternionKeys(), node, lAnimLayer);
			}
			//TODO: scale
			//data->getS
		}

	}

	void exportKFSequence(NiControllerSequenceRef ref) {
		lAnimStack = FbxAnimStack::Create(&scene, ref->GetName().c_str());
		FbxAnimLayer* layer = FbxAnimLayer::Create(&scene, "Default");
		for (ControlledBlock block : ref->GetControlledBlocks()) {
			//find the node
			string controlledBlockID;
			if (ref->GetStringPalette() != NULL) {
				controlledBlockID = getPalettedString(ref->GetStringPalette(), block.nodeNameOffset);
			}
			else {
				//default palette
				controlledBlockID = block.nodeName;
			}
			FbxNode* animatedNode = getBuiltNode(controlledBlockID/*, block.priority*/);

			if (animatedNode == NULL)
				throw runtime_error("exportKFSequence: Referenced node not found by name:" + controlledBlockID);
			//TODO: use palette here too
			//NiInterpolatorRef interpolator = block.interpolator;
			if (block.interpolator != NULL) {
				if (block.interpolator->IsDerivedType(NiTransformInterpolator::TYPE))
					addTrack(*DynamicCast<NiTransformInterpolator>(block.interpolator), animatedNode, layer);
				else
					addTrack(*block.interpolator, animatedNode, layer);
			}

		}
		lAnimStack->AddMember(layer);
	}

	void buildManagers() {
		//Usually the root node for animations sequences
		for (NiControllerManager* obj : managers) {
			//Next Controller can be ignored because it should be referred by individual sequences
			for (NiControllerSequenceRef ref : obj->GetControllerSequences()) {
				exportKFSequence(ref);
			}
		}
	}

public:

	FBXBuilderVisitor(NifFile& nif, FbxNode& sceneNode, FbxScene& scene, const NifInfo& info, const string& texture_path) :
		RecursiveFieldVisitor(*this, info),
		nif_file(nif),
		alreadyVisitedNodes(set<void*>()),
		this_info(info),
		scene(scene),
		managers(set<NiControllerManager*>()),
		texturePath(texture_path)
	{
		bhkScaleFactor = nif.GetBhkScaleFactor();
		NiNodeRef rootNode = DynamicCast<NiNode>(nif.GetRoot());
		if (rootNode != NULL)
		{

			build_stack.push_front(&sceneNode);
			root_name = rootNode->GetName();
			for (NiObjectRef child : rootNode->GetChildren())
				child->accept(*this, info);
			//visit managers too
			if (rootNode->GetController())
				rootNode->GetController()->accept(*this, info);
			if (rootNode->GetCollisionObject())
				rootNode->GetCollisionObject()->accept(*this, info);
			//TODO: handle different root types
			//Sort out skinning now
			processSkins();
			buildManagers();
		}
	}


	virtual inline void start(NiObject& in, const NifInfo& info) {
	}

	virtual inline void end(NiObject& in, const NifInfo& info) {
		if (alreadyVisitedNodes.find(&in) == alreadyVisitedNodes.end()) {
			build_stack.pop_front();
			alreadyVisitedNodes.insert(&in);
		}
	}

	//Always insert into the stack to be consistant
	template<class T>
	inline void visit_object(T& obj) {
		if (alreadyVisitedNodes.find(&obj) == alreadyVisitedNodes.end()) {
			FbxNode* node = visit_new_object(obj);
			if (node != NULL)
				build_stack.push_front(node);
			else
				build_stack.push_front(build_stack.front());
		}
	}

	template<class T>
	FbxNode* visit_new_object(T& object) {
		FbxNode* parent = build_stack.front();
		FbxNode* current = getBuiltNode(object);
		if (current == NULL) {
			current = build(object, parent);
			parent->AddChild(current);
		}
		return current;
	}

	//Deferred
	template<>
	FbxNode* visit_new_object(NiControllerManager& obj) {
		managers.insert(&obj);
		return NULL;
	}

	inline int find_material_index(const vector<bhkCMSDMaterial>& materials, bhkCMSDMaterial& material)
	{
		for (int i = 0; i < materials.size(); i++)
			if (materials[i].material == material.material && materials[i].filter == material.filter)
				return i;
		return -1;
	}

	//Collisions. Fbx has no support for shapes, so we must generate a geometry out of them

	FbxNode* recursive_convert(bhkShape* shape, FbxNode* parent, HavokFilter layer)
	{
		//Containers
		hkGeometry geom;
		string parent_name = parent->GetName();
		vector<bhkCMSDMaterial> materials;
		//bhkConvexTransformShape, bhkTransformShape
		if (shape->IsDerivedType(bhkTransformShape::TYPE))
		{
			parent_name += "_transform";
			bhkTransformShapeRef transform_block = DynamicCast<bhkTransformShape>(shape);
			transform_block->GetTransform();
			FbxNode* transform_node = FbxNode::Create(&scene, parent_name.c_str());
			parent->AddChild(transform_node);
			//setMatTransform(transform_block->GetTransform(), transform_node);
			recursive_convert(transform_block->GetShape(), transform_node, layer);
		}
		//bhkListShape, /bhkConvexListShape
		else if (shape->IsDerivedType(bhkListShape::TYPE))
		{
			parent_name += "_list";
			bhkListShapeRef list_block = DynamicCast<bhkListShape>(shape);
			FbxNode* list_node = FbxNode::Create(&scene, parent_name.c_str());
			parent->AddChild(list_node);
			const vector<bhkShapeRef>& shapes = list_block->GetSubShapes();
			for (int i = 0; i < shapes.size(); i++) {
				string name_index = parent_name + "_" + to_string(i);
				//FbxNode* child_shape_node = FbxNode::Create(&scene, name_index.c_str());
				//parent->AddChild(child_shape_node);
				recursive_convert(shapes[i], list_node, layer);
			}
		}
		else if (shape->IsDerivedType(bhkConvexListShape::TYPE))
		{
			parent_name += "_convex_list";
			bhkConvexListShapeRef list_block = DynamicCast<bhkConvexListShape>(shape);
			FbxNode* convex_list = FbxNode::Create(&scene, parent_name.c_str());
			parent->AddChild(convex_list);
			const vector<bhkConvexShapeRef>& shapes = list_block->GetSubShapes();
			for (int i = 0; i < shapes.size(); i++) {
				string name_index = parent_name + "_" + to_string(i);
				//FbxNode* child_shape_node = FbxNode::Create(&scene, name_index.c_str());
				//parent->AddChild(child_shape_node);
				recursive_convert(shapes[i], convex_list, layer);
			}
		}
		//bhkMoppBvTree
		else if (shape->IsDerivedType(bhkMoppBvTreeShape::TYPE))
		{
			parent_name += "_mopp";
			bhkMoppBvTreeShapeRef moppbv = DynamicCast<bhkMoppBvTreeShape>(shape);
			if (moppbv->GetShape() != NULL)
			{
				alreadyVisitedNodes.insert(moppbv);
				FbxNode* mopp_node = FbxNode::Create(&scene, parent_name.c_str());
				parent->AddChild(mopp_node);
				recursive_convert(moppbv->GetShape(), mopp_node, layer);				
			}
		}
		//shapes
		//bhkSphereShape
		else if (shape->IsDerivedType(bhkSphereShape::TYPE))
		{
			parent_name += "_sphere";
			bhkSphereShapeRef sphere = DynamicCast<bhkSphereShape>(shape);
			hkpSphereShape hkSphere(sphere->GetRadius());
			hkpShapeConverter::append(&geom, hkpShapeConverter::toSingleGeometry(&hkSphere));
			bhkCMSDMaterial material;
			material.material = sphere->GetMaterial().material_sk;
			material.filter = layer;
			int index = find_material_index(materials, material);
			if (index == -1)
			{
				index = materials.size();
				materials.push_back(material);
			}
			for (int i = 0; i < geom.m_triangles.getSize(); i++)
			{
				geom.m_triangles[i].m_material = index;
			}

		}
		//bhkBoxShape
		else if (shape->IsDerivedType(bhkBoxShape::TYPE))
		{
			parent_name += "_box";
			bhkBoxShapeRef box = DynamicCast<bhkBoxShape>(shape);
			hkpBoxShape hkBox(TOVECTOR4(box->GetDimensions()), box->GetRadius());
			hkpShapeConverter::append(&geom, hkpShapeConverter::toSingleGeometry(&hkBox));
			bhkCMSDMaterial material;
			material.material = box->GetMaterial().material_sk;
			material.filter = layer;
			int index = find_material_index(materials, material);
			if (index == -1)
			{
				index = materials.size();
				materials.push_back(material);
			}
			for (int i = 0; i < geom.m_triangles.getSize(); i++)
			{
				geom.m_triangles[i].m_material = index;
			}

		}
		//bhkCapsuleShape
		else if (shape->IsDerivedType(bhkCapsuleShape::TYPE))
		{
			parent_name += "_capsule";
			bhkCapsuleShapeRef capsule = DynamicCast<bhkCapsuleShape>(shape);
			hkpCapsuleShape hkCapsule(TOVECTOR4(capsule->GetFirstPoint()), TOVECTOR4(capsule->GetSecondPoint()), capsule->GetRadius());
			hkpShapeConverter::append(&geom, hkpShapeConverter::toSingleGeometry(&hkCapsule));
			bhkCMSDMaterial material;
			material.material = capsule->GetMaterial().material_sk;
			material.filter = layer;
			int index = find_material_index(materials, material);
			if (index == -1)
			{
				index = materials.size();
				materials.push_back(material);
			}
			for (int i = 0; i < geom.m_triangles.getSize(); i++)
			{
				geom.m_triangles[i].m_material = index;
			}
		}
		//bhkConvexVerticesShape
		else if (shape->IsDerivedType(bhkConvexVerticesShape::TYPE))
		{
			parent_name += "_convex";
			bhkConvexVerticesShapeRef convex = DynamicCast<bhkConvexVerticesShape>(shape);
			hkArray<hkVector4> vertices;
			for (const auto& vert : convex->GetVertices())
				vertices.pushBack(TOVECTOR4(vert));
			hkStridedVertices strided_vertices(vertices);
			hkpConvexVerticesShape hkConvex(strided_vertices);
			hkpShapeConverter::append(&geom, hkpShapeConverter::toSingleGeometry(&hkConvex));
			bhkCMSDMaterial material;
			material.material = convex->GetMaterial().material_sk;
			material.filter = layer;
			int index = find_material_index(materials, material);
			if (index == -1)
			{
				index = materials.size();
				materials.push_back(material);
			}
			for (int i = 0; i < geom.m_triangles.getSize(); i++)
			{
				geom.m_triangles[i].m_material = index;
			}
		}
		//bhkCompressedMeshShape
		else if (shape->IsDerivedType(bhkCompressedMeshShape::TYPE))
		{
			parent_name += "_mesh";
			bhkCompressedMeshShapeRef cmesh = DynamicCast<bhkCompressedMeshShape>(shape);
			alreadyVisitedNodes.insert(cmesh);
			alreadyVisitedNodes.insert(cmesh->GetData());
			Accessor<bhkCompressedMeshShapeData> converter(DynamicCast<bhkCompressedMeshShape>(cmesh), geom, materials);
		}
		//create collision materials
		for (const auto& material : materials)
		{
			string lMaterialName = NifFile::material_name(material.material);
			string collision_layer_name = NifFile::layer_name(material.filter.layer_sk);

			//FbxScene* scene = parent->GetScene();

			size_t materials_count = scene.GetMaterialCount();
			bool material_found = false;
			FbxSurfaceMaterial* m = NULL;
			for (int mm = 0; mm < materials_count; mm++)
			{
				m = scene.GetMaterial(mm);
				string m_name = m->GetName();
				FbxProperty layer = m->FindProperty("CollisionLayer");
				FbxString m_layer_v = layer.Get<FbxString>();
				string m_layer = m_layer_v.Buffer();
				if (m_name == lMaterialName && collision_layer_name == m_layer)
				{
					material_found = true;
					break;
				}
			}

			if (!material_found)
			{
				string lShadingName = "Phong";
				FbxDouble3 white = FbxDouble3(1, 1, 1);
				std::array<double, 3> m_color = NifFile::material_color(material.material);
				FbxDouble3 lcolor(m_color[0], m_color[1], m_color[2]);
				FbxDouble3 lDiffuseColor(0.75, 0.75, 0.0);
				FbxSurfacePhong* gMaterial = FbxSurfacePhong::Create(scene.GetFbxManager(), lMaterialName.c_str());
				//The following properties are used by the Phong shader to calculate the color for each pixel in the material :

				// Generate primary and secondary colors.
				gMaterial->Emissive = lcolor;
				gMaterial->TransparentColor = { 1.0,1.0,1.0 };
				gMaterial->TransparencyFactor = 0.9;
				gMaterial->Ambient = lcolor;
				gMaterial->Diffuse = lcolor;
				gMaterial->Shininess = 0.5;

				gMaterial->ShadingModel.Set(lShadingName.c_str());
				FbxProperty layer = FbxProperty::Create(gMaterial, FbxStringDT, "CollisionLayer");
				layer.Set(FbxString(collision_layer_name.c_str()));
				layer.ModifyFlag(FbxPropertyFlags::eUserDefined, true);
				m = gMaterial;
			}
			parent->AddMaterial(m);
		}

		//pack
		if (geom.m_vertices.getSize() > 0)
		{
			FbxNode* shape_node = FbxNode::Create(&scene, parent_name.c_str());
			parent_name += "_geometry";
			FbxMesh* m = FbxMesh::Create(&scene, parent_name.c_str());
			m->InitControlPoints(geom.m_vertices.getSize());
			FbxVector4* points = m->GetControlPoints();

			for (int i = 0; i < m->GetControlPointsCount(); i++) {
				points[i] = TOFBXVECTOR4(geom.m_vertices[i])*bhkScaleFactor;
			}

			
			FbxGeometryElementMaterial* lMaterialElement = m->CreateElementMaterial();
			lMaterialElement->SetMappingMode(FbxGeometryElement::eByPolygon);
			lMaterialElement->SetReferenceMode(FbxGeometryElement::eIndexToDirect);

			for (int i = 0; i < geom.m_triangles.getSize(); i++)
			{
				m->BeginPolygon(geom.m_triangles[i].m_material);
				m->AddPolygon(geom.m_triangles[i].m_a);
				m->AddPolygon(geom.m_triangles[i].m_b);
				m->AddPolygon(geom.m_triangles[i].m_c);
				m->EndPolygon();
			}
			
			shape_node->SetNodeAttribute(m);
			parent->AddChild(shape_node);
		}
		alreadyVisitedNodes.insert(shape);
		return parent;
	}

	class FbxConstraintBuilder {
		double bhkScaleFactor;

	protected:

		//TODO: pre 2010/660 import format!
		FbxNode* visit(RagdollDescriptor& descriptor, FbxNode* parent, FbxNode* child)
		{
			Vector4 row1 = descriptor.twistB;
			Vector4 row2 = descriptor.planeB;
			Vector4 row3 = descriptor.motorB;
			Vector4 row4 = descriptor.pivotB * bhkScaleFactor;

			Matrix44 mat = Matrix44(
				row1[0], row1[1], row1[2], row1[3],
				row2[0], row2[1], row2[2], row2[3],
				row3[0], row3[1], row3[2], row3[3],
				row4[0], row4[1], row4[2], row4[3]
			);
			FbxNode* constraint_node = FbxNode::Create(parent->GetScene(), string(string(parent->GetName()) + "_con_" + string(child->GetName())).c_str());
			parent->AddChild(constraint_node);
			setMatTransform(mat, constraint_node);

			FbxConstraintParent * fbx_constraint = FbxConstraintParent::Create(constraint_node, string(string(parent->GetName()) + "_con_" + string(child->GetName())).c_str());
			fbx_constraint->SetConstrainedObject(child);
			fbx_constraint->AddConstraintSource(constraint_node);
			fbx_constraint->AffectRotationX = false;
			fbx_constraint->AffectRotationY = false;
			fbx_constraint->AffectRotationZ = false;

			return constraint_node;
		}
		FbxNode* visit(PrismaticDescriptor& descriptor, FbxNode* parent, FbxNode* child) 
		{
			return parent;
		}
		FbxNode* visit(MalleableDescriptor& descriptor, FbxNode* parent, FbxNode* child)
		{
			switch (descriptor.type) {
				case BALLANDSOCKET:
					return visit(descriptor.ballAndSocket, parent, child);
				case HINGE:
					return visit(descriptor.hinge, parent, child);
				case LIMITED_HINGE:
					return visit(descriptor.limitedHinge, parent, child);
				case PRISMATIC:
					return visit(descriptor.prismatic, parent, child);
				case RAGDOLL:
					return visit(descriptor.ragdoll, parent, child);
				case STIFFSPRING:
					return visit(descriptor.stiffSpring, parent, child);
				case MALLEABLE:
					throw runtime_error("Recursive Malleable Constraint detected!");
			}
			throw runtime_error("Unknown Malleable Constraint detected!");
			return parent;
		}
		FbxNode* visit(HingeDescriptor& descriptor, FbxNode* parent, FbxNode* child) 
		{
			Vector4 row1 = descriptor.axleB;
			Vector4 row2 = descriptor.perp2AxleInB1;
			Vector4 row3 = descriptor.perp2AxleInB2;
			Vector4 row4 = descriptor.pivotB * bhkScaleFactor;

			Matrix44 mat = Matrix44(
				row1[0], row1[1], row1[2], row1[3],
				row2[0], row2[1], row2[2], row2[3],
				row3[0], row3[1], row3[2], row3[3],
				row4[0], row4[1], row4[2], row4[3]
			);
			FbxNode* constraint_node = FbxNode::Create(parent->GetScene(), string(string(parent->GetName()) + "_con_" + string(child->GetName())).c_str());
			parent->AddChild(setMatTransform(mat, constraint_node));

			FbxConstraintParent * fbx_constraint = FbxConstraintParent::Create(constraint_node, string(string(parent->GetName()) + "_con_" + string(child->GetName())).c_str());
			fbx_constraint->SetConstrainedObject(child);
			fbx_constraint->AddConstraintSource(constraint_node);
			fbx_constraint->AffectRotationX = false;

			return constraint_node;
		}
		FbxNode* visit(LimitedHingeDescriptor& descriptor, FbxNode* parent, FbxNode* child) 
		{
			Vector4 row1 = descriptor.axleB;
			Vector4 row2 = descriptor.perp2AxleInB1;
			Vector4 row3 = descriptor.perp2AxleInB2;
			Vector4 row4 = descriptor.pivotB * bhkScaleFactor;

			Matrix44 mat = Matrix44(
				row1[0], row1[1], row1[2], row1[3],
				row2[0], row2[1], row2[2], row2[3],
				row3[0], row3[1], row3[2], row3[3],
				row4[0], row4[1], row4[2], row4[3]
			);
			FbxNode* constraint_node = FbxNode::Create(parent->GetScene(), string(string(parent->GetName()) + "_con_" + string(child->GetName())).c_str());
			parent->AddChild(setMatTransform(mat, constraint_node));

			FbxConstraintParent * fbx_constraint = FbxConstraintParent::Create(constraint_node, string(string(parent->GetName()) + "_con_" + string(child->GetName())).c_str());
			fbx_constraint->SetConstrainedObject(child);
			fbx_constraint->AddConstraintSource(constraint_node);
			fbx_constraint->AffectRotationX = false;

			return constraint_node;
		}
		FbxNode* visit(BallAndSocketDescriptor& descriptor, FbxNode* parent, FbxNode* child) 
		{
			return parent;
		}
		FbxNode* visit(StiffSpringDescriptor& descriptor, FbxNode* parent, FbxNode* child) 
		{
			return parent;
		}

		void visitConstraint(FbxNode* holder, map<bhkRigidBodyRef, FbxNode*>& bodies, bhkRigidBodyRef parent, bhkConstraintRef constraint) {
			if (constraint)
			{
				for (const auto& entity : constraint->GetEntities())
				{
					if (entity->IsDerivedType(bhkRigidBody::TYPE))
					{
						bhkRigidBodyRef rbentity = DynamicCast<bhkRigidBody>(entity);
						if (rbentity != parent)
						{
							if (bodies.find(rbentity) == bodies.end())
								throw runtime_error("Wrong Nif Hierarchy, entity referred before being built!");
							FbxNode* parent = bodies[rbentity];


							//Constraints need an animation stack?
							FbxScene* scene = holder->GetScene();
							size_t stacks = scene->GetSrcObjectCount<FbxAnimStack>();
							if (stacks == 0)
							{
								FbxAnimStack* lAnimStack = FbxAnimStack::Create(scene, "Take 001");
								FbxAnimLayer* layer = FbxAnimLayer::Create(scene, "Default");
							}

							FbxNode* constraint_position = NULL;

							if (constraint->IsSameType(bhkRagdollConstraint::TYPE))
								constraint_position = visit(DynamicCast<bhkRagdollConstraint>(constraint)->GetRagdoll(), parent, holder);
							else if (constraint->IsSameType(bhkPrismaticConstraint::TYPE))
								constraint_position = visit(DynamicCast<bhkPrismaticConstraint>(constraint)->GetPrismatic(), parent, holder);
							else if (constraint->IsSameType(bhkMalleableConstraint::TYPE))
								constraint_position = visit(DynamicCast<bhkMalleableConstraint>(constraint)->GetMalleable(), parent, holder);
							else if (constraint->IsSameType(bhkHingeConstraint::TYPE))
								constraint_position = visit(DynamicCast<bhkHingeConstraint>(constraint)->GetHinge(), parent, holder);
							else if (constraint->IsSameType(bhkLimitedHingeConstraint::TYPE))
								constraint_position = visit(DynamicCast<bhkLimitedHingeConstraint>(constraint)->GetLimitedHinge(), parent, holder);
							else if (constraint->IsSameType(bhkBallAndSocketConstraint::TYPE))
								constraint_position = visit(DynamicCast<bhkBallAndSocketConstraint>(constraint)->GetBallAndSocket(), parent, holder);
							else if (constraint->IsSameType(bhkStiffSpringConstraint::TYPE))
								constraint_position = visit(DynamicCast<bhkStiffSpringConstraint>(constraint)->GetStiffSpring(), parent, holder);
							else
								throw new runtime_error("Unimplemented constraint type!");

							//FbxConstraintParent * fbx_constraint = FbxConstraintParent::Create(constraint_position, string(string(parent->GetName()) + "_con_" + string(holder->GetName())).c_str());
							//fbx_constraint->SetConstrainedObject(holder);
							//fbx_constraint->AddConstraintSource(constraint_position);
							//fbx_constraint->AffectRotationX = false;
							//fbx_constraint->AffectRotationY = false;
							//fbx_constraint->AffectRotationZ = false;
						}
					}
				}	
			}
		}

	public:

		FbxConstraintBuilder(FbxNode* holder, map<bhkRigidBodyRef, FbxNode*>& bodies, bhkRigidBodyRef parent, bhkConstraintRef constraint, double bhkScaleFactor) : bhkScaleFactor(bhkScaleFactor)
		{
			visitConstraint(holder, bodies, parent, constraint);
		}
		
	};

	inline FbxNode* visit_rigid_body(bhkRigidBodyRef obj, const string& collision_name) {
		FbxNode* parent = build_stack.front();
		string name = collision_name;
		name += "_rb";
		FbxNode* rb_node = FbxNode::Create(&scene, name.c_str());
		bodies[obj] = rb_node;

		//This is funny: Rigid Bodies transform is an actual world matrix even if 
		//it's parented into a NiNode. The value is actually ignored without a T derived class
		//So while the creature is not in a ragdoll state, the rigid bodies are driven by the ninode;
		//When the ragdoll kicks in, the transformations are matched
		//Havok engine would originally separate the skeletal animation and the ragdoll,
		//but that will require an additional linking between the ragdoll and the parent bone
		//to avoid this, we are properly parenting the rigid body and calculate a relative matrix on import
		//the T condition is left for reference

		/*if (obj->IsSameType(bhkRigidBodyT::TYPE))
		{*/
			Vector4 translation = obj->GetTranslation();
			rb_node->LclTranslation.Set(FbxDouble3(translation.x*bhkScaleFactor, translation.y*bhkScaleFactor, translation.z*bhkScaleFactor));
			Niflib::hkQuaternion rotation = obj->GetRotation();
			Quat QuatTest = { rotation.x, rotation.y, rotation.z, rotation.w };
			EulerAngles inAngs = Eul_FromQuat(QuatTest, EulOrdXYZs);
			rb_node->LclRotation.Set(FbxVector4(rad2deg(inAngs.x), rad2deg(inAngs.y), rad2deg(inAngs.z)));
		//}
		recursive_convert(obj->GetShape(), rb_node, obj->GetHavokFilter());

		//find the relative transform between nodes
		FbxAMatrix world_parent = parent->EvaluateGlobalTransform();
		FbxAMatrix world_child = rb_node->EvaluateGlobalTransform();

		FbxAMatrix rel = world_parent.Inverse() * world_child;

		parent->AddChild(setMatATransform(rel, rb_node));

		//Constraints
		for (const auto& constraint : obj->GetConstraints())
		{
			if (constraint->IsDerivedType(bhkConstraint::TYPE)) {
				FbxConstraintBuilder(rb_node, bodies, obj, DynamicCast<bhkConstraint>(constraint), bhkScaleFactor);
				alreadyVisitedNodes.insert(constraint);
			}
			else {
				throw runtime_error("Unknown constraint hierarchy!");
			}
		}

		return rb_node;
	}

	inline FbxNode* visit_phantom(bhkSimpleShapePhantomRef obj, const string& collision_name) {
		FbxNode* parent = build_stack.front();
		string name = collision_name;
		name += "_sp";
		FbxNode* rb_node = FbxNode::Create(&scene, name.c_str());
		rb_node->SetShadingMode(FbxNode::EShadingMode::eWireFrame);

		Matrix44 transform = obj->GetTransform();
		//Following nifskope, this is 
		//setMatTransform(transform, rb_node, bhkScaleFactor);
		recursive_convert(obj->GetShape(), rb_node, obj->GetHavokFilter());

		//hmmmm... seems to be relative and not absolute as in the case of rigid bodies
		//find the relative transform between nodes
		//FbxAMatrix world_parent = parent->EvaluateGlobalTransform();
		//FbxAMatrix world_child = rb_node->EvaluateGlobalTransform();

		//FbxAMatrix rel = world_parent.Inverse() * world_child;

		parent->AddChild(/*setMatATransform(rel, */rb_node/*)*/);

		return rb_node;
	}

	template<>
	inline FbxNode* visit_new_object(bhkCollisionObject& obj) {
		FbxNode* parent = build_stack.front();
		string name = obj.GetTarget()->GetName();
		sanitizeString(name);
		if (obj.GetBody() && obj.GetBody()->IsDerivedType(bhkRigidBody::TYPE))
		{
			alreadyVisitedNodes.insert(obj.GetBody());
			return visit_rigid_body(DynamicCast<bhkRigidBody>(obj.GetBody()), name);
		}
		return NULL;
	}

	template<>
	inline FbxNode* visit_new_object(bhkBlendCollisionObject& obj) {
		FbxNode* parent = build_stack.front();
		string name = obj.GetTarget()->GetName();
		sanitizeString(name);
		if (obj.GetBody() && obj.GetBody()->IsDerivedType(bhkRigidBody::TYPE))
		{
			alreadyVisitedNodes.insert(obj.GetBody());
			return visit_rigid_body(DynamicCast<bhkRigidBody>(obj.GetBody()), name);
		}
		return NULL;
	}

	template<>
	inline FbxNode* visit_new_object(bhkSPCollisionObject& obj) {
		FbxNode* parent = build_stack.front();
		string name = obj.GetTarget()->GetName();
		sanitizeString(name);
		if (obj.GetBody() && obj.GetBody()->IsDerivedType(bhkSimpleShapePhantom::TYPE))
		{
			alreadyVisitedNodes.insert(obj.GetBody());
			return visit_phantom(DynamicCast<bhkSimpleShapePhantom>(obj.GetBody()), name);
		}
		return NULL;
	}

	template<class T>
	inline void visit_compound(T& obj) {}

	template<class T>
	inline void visit_field(T& obj) {}

	template<class T>
	FbxNode* build(T& obj, FbxNode* parent) {
		NiObject* pobj = (NiObject*)&obj;
		if (pobj->IsDerivedType(NiAVObject::TYPE)) {
			NiAVObjectRef av = DynamicCast<NiAVObject>(pobj);
			string name = av->GetName().c_str(); sanitizeString(name);
			FbxNode* node = FbxNode::Create(&scene, name.c_str());
			return setTransform(av, node);
		}
		return setNullTransform(
			FbxNode::Create(&scene, pobj->GetInternalType().GetTypeName().c_str())
		);
	}

	template<>
	FbxNode* build(NiTriShape& obj, FbxNode* parent) {
		//need to import the whole tree structure before skinning, so defer into a list
		if (obj.GetSkinInstance() != NULL)
			skins[obj.GetSkinInstance()] = &obj;
		return setTransform(&obj, AddGeometry(obj));
	}

	template<>
	FbxNode* build(NiTriStrips& obj, FbxNode* parent) {
		//need to import the whole tree structure before skinning, so defer into a list
		if (obj.GetSkinInstance() != NULL)
			skins[obj.GetSkinInstance()] = &obj;
		return setTransform(&obj, AddGeometry(obj));
	}
};

void FBXWrangler::AddNif(NifFile& nif) {
	FBXBuilderVisitor(nif, *scene->GetRootNode(), *scene, nif.GetInfo(), texture_path);
}

bool FBXWrangler::ExportScene(const std::string& fileName) {
	FbxExporter* iExporter = FbxExporter::Create(sdkManager, "");
	if (!iExporter->Initialize(fileName.c_str(), -1, sdkManager->GetIOSettings())) {
		iExporter->Destroy();
		return false;
	}

	// Export options determine what kind of data is to be imported.
	// The default (except for the option eEXPORT_TEXTURE_AS_EMBEDDED)
	// is true, but here we set the options explicitly.
	FbxIOSettings* ios = sdkManager->GetIOSettings();
	ios->SetBoolProp(EXP_FBX_MATERIAL, true);
	ios->SetBoolProp(EXP_FBX_TEXTURE, true);
	ios->SetBoolProp(EXP_FBX_EMBEDDED, false);
	ios->SetBoolProp(EXP_FBX_SHAPE, true);
	ios->SetBoolProp(EXP_FBX_GOBO, true);
	ios->SetBoolProp(EXP_FBX_CONSTRAINT, true);
	ios->SetBoolProp(EXP_FBX_ANIMATION, true);
	ios->SetBoolProp(EXP_FBX_GLOBAL_SETTINGS, true);

	iExporter->SetFileExportVersion(FBX_2014_00_COMPATIBLE, FbxSceneRenamer::eNone);

	sdkManager->CreateMissingBindPoses(scene);

	//FbxAxisSystem axis(FbxAxisSystem::eMax);
	//axis.ConvertScene(scene);

	bool status = iExporter->Export(scene);
	iExporter->Destroy();

	return status;
}

bool FBXWrangler::ImportScene(const std::string& fileName, const FBXImportOptions& options) {
	FbxIOSettings* ios = sdkManager->GetIOSettings();
	ios->SetBoolProp(IMP_FBX_MATERIAL, true);
	ios->SetBoolProp(IMP_FBX_TEXTURE, true);
	ios->SetBoolProp(IMP_FBX_LINK, false);
	ios->SetBoolProp(IMP_FBX_SHAPE, true);
	ios->SetBoolProp(IMP_FBX_GOBO, true);
	ios->SetBoolProp(IMP_FBX_ANIMATION, true);
	ios->SetBoolProp(IMP_FBX_GLOBAL_SETTINGS, true);

	FbxImporter* iImporter = FbxImporter::Create(sdkManager, "");
	if (!iImporter->Initialize(fileName.c_str(), -1, ios)) {
		iImporter->Destroy();
		return false;
	}

	if (scene)
		CloseScene();

	scene = FbxScene::Create(sdkManager, fileName.c_str());

	bool status = iImporter->Import(scene);

	FbxAxisSystem maxSystem(FbxAxisSystem::EUpVector::eZAxis, (FbxAxisSystem::EFrontVector) - 2, FbxAxisSystem::ECoordSystem::eRightHanded);
	maxSystem.ConvertScene(scene);

	if (!status) {
		FbxStatus ist = iImporter->GetStatus();
		iImporter->Destroy();
		return false;
	}
	iImporter->Destroy();

	return LoadMeshes(options);
}

class RebuildVisitor : public RecursiveFieldVisitor<RebuildVisitor> {
	set<NiObject*> objects;
public:
	vector<NiObjectRef> blocks;

	RebuildVisitor(NiObject* root, const NifInfo& info) :
		RecursiveFieldVisitor(*this, info) {
		root->accept(*this, info);

		for (NiObject* ptr : objects) {
			blocks.push_back(ptr);
		}
	}


	template<class T>
	inline void visit_object(T& obj) {
		objects.insert(&obj);
	}


	template<class T>
	inline void visit_compound(T& obj) {
	}

	template<class T>
	inline void visit_field(T& obj) {}
};

bool hasNoTransform(FbxNode* node) {
	FbxDouble3 trans = node->LclTranslation.Get();
	FbxDouble3 euler_angles = node->LclRotation.Get();
	FbxDouble3 scaling = node->LclScaling.Get();
	
	return abs(trans[0]) < 1e-5 &&
		abs(trans[1]) < 1e-5 &&
		abs(trans[2]) < 1e-5 &&
		abs(euler_angles[0]) < 1e-5 &&
		abs(euler_angles[1]) < 1e-5 &&
		abs(euler_angles[2]) < 1e-5 &&
		abs(scaling[0]) > 1 - 1e-5 && abs(scaling[0]) < 1 + 1e-5 &&
		abs(scaling[1]) > 1 - 1e-5 && abs(scaling[1]) < 1 + 1e-5 &&
		abs(scaling[2]) > 1 - 1e-5 && abs(scaling[2]) < 1 + 1e-5;
}



FbxAMatrix getNodeTransform(FbxNode* pNode, bool absolute = false) {
	FbxAMatrix matrixGeo;
	matrixGeo.SetIdentity();
	if (pNode->GetNodeAttribute())
	{
		const FbxVector4 lT = pNode->GetGeometricTranslation(FbxNode::eSourcePivot);
		const FbxVector4 lR = pNode->GetGeometricRotation(FbxNode::eSourcePivot);
		const FbxVector4 lS = pNode->GetGeometricScaling(FbxNode::eSourcePivot);
		matrixGeo.SetT(lT);
		matrixGeo.SetR(lR);
		matrixGeo.SetS(lS);
	}
	FbxAMatrix localMatrix = pNode->EvaluateLocalTransform();
	if (absolute)
		localMatrix = pNode->EvaluateGlobalTransform();

	return localMatrix * matrixGeo;
}

FbxAMatrix getWorldTransform(FbxNode* pNode) {
	FbxAMatrix matrixGeo;
	matrixGeo.SetIdentity();
	if (pNode->GetNodeAttribute())
	{
		const FbxVector4 lT = pNode->GetGeometricTranslation(FbxNode::eSourcePivot);
		const FbxVector4 lR = pNode->GetGeometricRotation(FbxNode::eSourcePivot);
		const FbxVector4 lS = pNode->GetGeometricScaling(FbxNode::eSourcePivot);
		matrixGeo.SetT(lT);
		matrixGeo.SetR(lR);
		matrixGeo.SetS(lS);
	}
	FbxAMatrix localMatrix = pNode->EvaluateLocalTransform();

	FbxNode* pParentNode = pNode->GetParent();
	FbxAMatrix parentMatrix = pParentNode->EvaluateLocalTransform();
	while ((pParentNode = pParentNode->GetParent()) != NULL)
	{
		parentMatrix = pParentNode->EvaluateLocalTransform() * parentMatrix;
	}

	return parentMatrix * localMatrix * matrixGeo;
}

void setAvTransform(FbxNode* node, NiAVObject* av, bool rotation_only = false, bool absolute = false) {
	FbxAMatrix tr = getNodeTransform(node, absolute);
	FbxDouble3 trans = tr.GetT();
	av->SetTranslation(Vector3(trans[0], trans[1], trans[2]));
	av->SetRotation(
		Matrix33(
			tr.Get(0,0), tr.Get(0,1), tr.Get(0,2),
			tr.Get(1,0), tr.Get(1,1), tr.Get(1,2),
			tr.Get(2,0), tr.Get(2,1), tr.Get(2,2)
		)
	);
	FbxDouble3 scaling = tr.GetS(); // node->LclScaling.Get();
	if (scaling[0] == scaling[1] && scaling[1] == scaling[2])
		av->SetScale(scaling[0]);
}

NiTransform GetAvTransform(FbxAMatrix tr) {
	NiTransform out;
	FbxDouble3 trans = tr.GetT();
	out.translation = Vector3(trans[0], trans[1], trans[2]);
	out.rotation =
		Matrix33(
			tr.Get(0, 0), tr.Get(0, 1), tr.Get(0, 2),
			tr.Get(1, 0), tr.Get(1, 1), tr.Get(1, 2),
			tr.Get(2, 0), tr.Get(2, 1), tr.Get(2, 2)
		)
	;
	FbxDouble3 scaling = tr.GetS(); // node->LclScaling.Get();
	if (scaling[0] == scaling[1] && scaling[1] == scaling[2])
		out.scale = scaling[0];
	return out;
}


struct bone_weight {
	NiNode* bone;
	float weight;

	bone_weight(NiNode* bone, float weight) : bone(bone), weight(weight) {}
};

class AccessSkin {};

template<>
class Accessor<AccessSkin>
{
	public:
		Accessor(FbxMesh* m, NiTriShapeRef shape, map<FbxNode*, NiObjectRef>& conversion_Map, NiNodeRef& conversion_root, bool export_skin) {
			if (m->GetDeformerCount(FbxDeformer::eSkin) > 0) {
				NiSkinInstanceRef skin;
				if (export_skin)
					skin = new BSDismemberSkinInstance();
				else
					skin = new NiSkinInstance();
				NiSkinDataRef data = new NiSkinData();
				NiSkinPartitionRef spartition = new NiSkinPartition();

				map<NiNode*, BoneData > bones_data;
				vector<SkinPartition > partitions;
				//set<NiNode*> bones;
				//vector<NiNode*> vbones;

				multimap<int, int> faces_map;

				int numTris = m->GetPolygonCount();
				for (int t = 0; t < numTris; t++) {
					if (m->GetPolygonSize(t) != 3)
						continue;

					int p1 = m->GetPolygonVertex(t, 0);
					int p2 = m->GetPolygonVertex(t, 1);
					int p3 = m->GetPolygonVertex(t, 2);

					faces_map.insert({ p1,t });
					faces_map.insert({ p2,t });
					faces_map.insert({ p3,t });

				}

				for (int iSkin = 0; iSkin < m->GetDeformerCount(FbxDeformer::eSkin); iSkin++) {
					FbxSkin* fbx_skin = (FbxSkin*)m->GetDeformer(iSkin, FbxDeformer::eSkin);
					SkinPartition partition;
					
					for (int iCluster = 0; iCluster < fbx_skin->GetClusterCount(); iCluster++) {
						FbxCluster* cluster = fbx_skin->GetCluster(iCluster);
						if (!cluster->GetLink())
							continue;

						FbxNode* bone = cluster->GetLink();
						NiNodeRef ni_bone = DynamicCast<NiNode>(conversion_Map[cluster->GetLink()]);

						//find global
						vector<NiNode * >& bones = skin->bones;
						vector<BoneData >& bone_data_list = data->boneList;
						vector<NiNode * >::iterator bone_it = find(bones.begin(), bones.end(), ni_bone);
						size_t bone_data_index = -1;
						if (bone_it != bones.end())
						{
							bone_data_index = distance(bones.begin(), bone_it);
						}
						else {
							bone_data_index = bones.size();
							bones.push_back(ni_bone);
							BoneData bd;

							FbxAMatrix joint_transform; cluster->GetTransformLinkMatrix(joint_transform);
							FbxAMatrix global_transform; global_transform = cluster->GetTransformMatrix(global_transform).Inverse();
							FbxAMatrix local_transform = (global_transform * joint_transform).Inverse();

							data->skinTransform = GetAvTransform(global_transform);
							bd.skinTransform = GetAvTransform(local_transform);
							bone_data_list.push_back(bd);
							//data->SetBoneList(bone_data_list);
						}
						//Bone Data
						BoneData& bone_data = bone_data_list[bone_data_index];

						partition.numWeightsPerVertex = 4;

						//find local values
						vector<unsigned short >& partition_bones = partition.bones;
						size_t local_bone_index = -1;
						vector<unsigned short >::iterator local_bones_it = find(partition_bones.begin(), partition_bones.end(), bone_data_index);
						if (local_bones_it == partition_bones.end())
						{
							local_bone_index = partition_bones.size();
							partition_bones.push_back(bone_data_index);
							partition.numBones++;
						}
						else {
							local_bone_index = distance(partition_bones.begin(), local_bones_it);
						}

						size_t cp_count = cluster->GetControlPointIndicesCount();
						vector< vector<float > >& local_vertices_weights = partition.vertexWeights;
						//local_vertices_weights.resize(cp_count);
						vector< vector<byte > >& local_vertices_indices = partition.boneIndices;
						//local_vertices_indices.resize(cp_count);
						vector<Triangle>& local_triangles = partition.triangles;
						for (int iPoint = 0; iPoint < cluster->GetControlPointIndicesCount(); iPoint++)
						{
							int v = cluster->GetControlPointIndices()[iPoint];
							float w = cluster->GetControlPointWeights()[iPoint];

							if (w > 0.0)
							{
								BoneVertData bvd;
								bvd.index = v;
								bvd.weight = w;
							
								bone_data.vertexWeights.push_back(bvd);
								bone_data.numVertices++;
							}

							size_t local_vertex_index = -1;
							vector<unsigned short >::iterator v_local_it = find(partition.vertexMap.begin(), partition.vertexMap.end(), v);

							if (v_local_it == partition.vertexMap.end()) {
								partition.numVertices++;
								
								local_vertex_index = partition.vertexMap.size();
								partition.vertexMap.push_back(v);
								local_vertices_weights.resize(partition.vertexMap.size());
								local_vertices_indices.resize(partition.vertexMap.size());
							}
							else {
								local_vertex_index = distance(partition.vertexMap.begin(), v_local_it);
							}

							vector<float >& local_vertex_weight_array = local_vertices_weights[local_vertex_index];
							local_vertex_weight_array.push_back(w);
							vector<byte >& local_bones_indices = local_vertices_indices[local_vertex_index];
							local_bones_indices.push_back(local_bone_index);							
						} //end points

						//triangles
						for (int iPoint = 0; iPoint < cluster->GetControlPointIndicesCount(); iPoint++)
						{
							int v = cluster->GetControlPointIndices()[iPoint];
							pair <multimap<int, int>::iterator, multimap<int, int>::iterator> this_faces = faces_map.equal_range(v);

							for (multimap<int, int>::iterator faces_it = this_faces.first; faces_it != this_faces.second; faces_it++)
							{
								int triangle_index = faces_it->second;

								int global_v1 = m->GetPolygonVertex(triangle_index, 0);
								int global_v2 = m->GetPolygonVertex(triangle_index, 1);
								int global_v3 = m->GetPolygonVertex(triangle_index, 2);

								vector<unsigned short >::iterator local_v1_it = find(partition.vertexMap.begin(), partition.vertexMap.end(), global_v1);
								vector<unsigned short >::iterator local_v2_it = find(partition.vertexMap.begin(), partition.vertexMap.end(), global_v2);
								vector<unsigned short >::iterator local_v3_it = find(partition.vertexMap.begin(), partition.vertexMap.end(), global_v3);

								if (local_v1_it != partition.vertexMap.end() && local_v2_it != partition.vertexMap.end() && local_v3_it != partition.vertexMap.end())
								{
									int local_v1 = distance(partition.vertexMap.begin(), local_v1_it);
									int local_v2 = distance(partition.vertexMap.begin(), local_v2_it);
									int local_v3 = distance(partition.vertexMap.begin(), local_v3_it);

									Triangle ni_t = { (unsigned short)local_v1,
										(unsigned short)local_v2,
										(unsigned short)local_v3 };

									vector<Triangle>::iterator tris_pos = find(local_triangles.begin(), local_triangles.end(), ni_t);
									if (tris_pos == local_triangles.end())
									{
										local_triangles.push_back(ni_t);
										partition.numTriangles++;
									}
								}
							}
						}

						for (int i_f = 0; i_f < partition.numVertices; i_f++) {

							vector<byte >& pindex = local_vertices_indices[i_f];
							if (pindex.size() > 4)
								Log::Error("Too many indices for bone");

							vector<float >& vweight = local_vertices_weights[i_f];
							if (vweight.size() > 4)
								Log::Error("Too many weights for bone");

							//find less influencing bone
							if (pindex.size() != vweight.size()) {
								Log::Error("Fatal Error: weights != indexes");
								throw std::runtime_error("Fatal Error: weights != indexes");
							}
							bool removed = false;
							while (vweight.size() > 4) {
								removed = true;
								int to_remove_bone = distance(vweight.begin(), min_element(begin(vweight), end(vweight)));
								pindex.erase(pindex.begin() + to_remove_bone);
								vweight.erase(vweight.begin() + to_remove_bone);
							}
							if (removed) {
								float sum = std::accumulate(vweight.begin(), vweight.end(), 0.0f);
								vweight[0] /= sum;
								vweight[1] /= sum;
								vweight[2] /= sum;
								vweight[3] /= sum;
							}

							pindex.resize(4);
							vweight.resize(4);
						}
					}

					if (partition.bones.size()<4)
						partition.bones.resize(4);

					partition.numWeightsPerVertex = 4;
					partition.numTriangles = partition.triangles.size();
					partition.hasFaces = partition.triangles.size() > 0;
					partition.trianglesCopy = partition.triangles;

					partition.hasVertexMap = partition.vertexMap.size() > 0;
					partition.hasVertexWeights = partition.vertexWeights.size() > 0;
					partition.hasBoneIndices = partition.boneIndices.size() > 0;

					partitions.push_back(partition);
				}

				spartition->SetSkinPartitionBlocks(partitions);
				data->SetHasVertexWeights(1);
				skin->SetData(data);
				skin->SetSkinPartition(spartition);
				skin->SetSkeletonRoot(conversion_root);

				if (export_skin)
				{
					BSDismemberSkinInstanceRef bsskin = DynamicCast<BSDismemberSkinInstance>(skin);
					bsskin->partitions.resize(bsskin->skinPartition->skinPartitionBlocks.size());
					for (int i = 0; i < bsskin->partitions.size(); i++)
					{
						bsskin->partitions[i].bodyPart = SBP_32_BODY;
						bsskin->partitions[i].partFlag = (BSPartFlag)( PF_EDITOR_VISIBLE | PF_START_NET_BONESET);
					}
				}

				shape->SetSkinInstance(skin);
			}

		}
};

void FBXWrangler::convertSkins(FbxMesh* m, NiTriShapeRef shape) {
	Accessor<AccessSkin> do_it(m, shape, conversion_Map, conversion_root, export_skin);
	int bones = 60;
	int weights = 4;
	remake_partitions(StaticCast<NiTriBasedGeom>(shape), bones, weights, false, false);
}

template <class Type, typename T> 
T getIndexedElement(FbxLayerElementTemplate<Type>* layer, size_t index)
{
	switch (layer->GetReferenceMode()) {
		case FbxLayerElement::EReferenceMode::eDirect:
			return layer->GetDirectArray().GetAt(index);
		case FbxLayerElement::eIndex:
		case FbxLayerElement::eIndexToDirect:
			return layer->GetDirectArray().GetAt(layer->GetIndexArray()->GetAt(index));
	}
}

template <class Type, typename T>
void getElement(FbxLayerElementTemplate<Type>* layer, size_t index, T& element) {
	switch (layer->GetMappingMode())
	{
	case FbxLayerElement::EMappingMode::eByControlPoint:
		T = getIndexedElement(layer, index)
	case FbxLayerElement::EMappingMode::eByPolygonVertex:
	case FbxLayerElement::EMappingMode::eByPolygon:
	case FbxLayerElement::EMappingMode::eByEdge:
	case FbxLayerElement::EMappingMode::eAllSame:
	}
}

NiTriShapeRef FBXWrangler::importShape(const string& name, FbxNodeAttribute* node, const FBXImportOptions& options) {
	NiTriShapeRef out = new NiTriShape();
	NiTriShapeDataRef data = new NiTriShapeData();

	bool hasAlpha = false;

	FbxMesh* m = (FbxMesh*)node;
	FbxGeometryElementUV* uv = m->GetElementUV(0);
	FbxGeometryElementNormal* normal = m->GetElementNormal(0);
	FbxGeometryElementVertexColor* vc = m->GetElementVertexColor(0);

	string orig_name = name;
	out->SetName(unsanitizeString(orig_name));
	int numVerts = m->GetControlPointsCount();
	int numTris = m->GetPolygonCount();

	vector<Vector3> verts;
	vector<Vector3> normals;
	vector<Color4 > vcs;

	if (normal == NULL) {
		Log::Info("Warning: cannot find normals, I'll recalculate them for %s", name.c_str());
	}
	vector<TexCoord> uvs;

	FbxAMatrix node_trans; node_trans = m->GetPivot(node_trans);

	for (int v = 0; v < numVerts; v++) {
		FbxVector4 vert = m->GetControlPointAt(v);

		if (uv && uv->GetMappingMode() != FbxGeometryElement::eByControlPoint &&
			uv->GetMappingMode() != FbxGeometryElement::eByPolygonVertex) {
			FbxGeometryElement::EMappingMode m = uv->GetMappingMode();
		}

		verts.emplace_back((float)vert.mData[0], (float)vert.mData[1], (float)vert.mData[2]);
		if (uv && uv->GetMappingMode() == FbxGeometryElement::eByControlPoint) {
			int uIndex = v;
			if (uv->GetReferenceMode() == FbxLayerElement::eIndexToDirect)
				uIndex = uv->GetIndexArray().GetAt(v);

			uvs.emplace_back((float)uv->GetDirectArray().GetAt(uIndex).mData[0],
				(float)uv->GetDirectArray().GetAt(uIndex).mData[1]);
		}

		if (normal && normal->GetMappingMode() == FbxGeometryElement::eByControlPoint) {
			normals.emplace_back((float)normal->GetDirectArray().GetAt(v).mData[0],
				(float)normal->GetDirectArray().GetAt(v).mData[1],
				(float)normal->GetDirectArray().GetAt(v).mData[2]);
		}

		if (vc && vc->GetMappingMode() == FbxGeometryElement::eByControlPoint) {
			vcs.emplace_back(
				Color4
				(
				(float)vc->GetDirectArray().GetAt(v).mRed,
					(float)vc->GetDirectArray().GetAt(v).mGreen,
					(float)vc->GetDirectArray().GetAt(v).mBlue,
					(float)vc->GetDirectArray().GetAt(v).mAlpha
				)
			);
			if (hasAlpha == false && (float)vc->GetDirectArray().GetAt(v).mAlpha < 1.0)
				hasAlpha = true;
		}
	}



	const char* uvName = nullptr;
	if (uv) {
		uvName = uv->GetName();
		uvs.resize(numVerts);
	}

	if (vc && vcs.empty()) {
		vcs.resize(numVerts);
	}

	if (normal && normals.empty())
		normals.resize(numVerts);

	vector<Triangle> tris;
	set<int> mapped;

	for (int t = 0; t < numTris; t++) {
		if (m->GetPolygonSize(t) != 3)
			continue;

		std::array<int, 3> triangle;

		for (int i = 0; i < 3; i++)
		{
			FbxVector4 v_n;
			FbxVector2 v_uv;
			FbxColor color;

			bool has_normal = false;
			bool has_uv = false;
			bool has_vc = false;

			Vector3 nif_n;
			TexCoord nif_uv;
			Color4 nif_color;

			bool isUnmapped;
			int vertex_index = m->GetPolygonVertex(t, i);

			if (normal && normal->GetMappingMode() == FbxGeometryElement::eByPolygonVertex) {
				m->GetPolygonVertexNormal(t, i, v_n);
				nif_n = Vector3(v_n.mData[0], v_n.mData[1], v_n.mData[2]);
				has_normal = true;
			}
			if (uv && uv->GetMappingMode() == FbxGeometryElement::eByPolygonVertex) {
				m->GetPolygonVertexUV(t, i, uvName, v_uv, isUnmapped);
				nif_uv = TexCoord(v_uv.mData[0], v_uv.mData[1]);
				has_uv = true;
			}
			if (vc && vc->GetMappingMode() == FbxGeometryElement::eByPolygonVertex)
			{
				switch (vc->GetReferenceMode())
				{
				case FbxGeometryElement::eDirect:
				{
					color = vc->GetDirectArray().GetAt(t * 3 + i);
					break;
				}
				case FbxGeometryElement::eIndexToDirect:
				{
					int id = vc->GetIndexArray().GetAt(t * 3 + i);
					color = vc->GetDirectArray().GetAt(id);
					break;
				}
				default:
				{
					break;
				}
				}
				if (hasAlpha == false && color.mAlpha < 1.0)
					hasAlpha = true;
				nif_color = Color4
				(
					(float)color.mRed,
					(float)color.mGreen,
					(float)color.mBlue,
					(float)color.mAlpha
				);
				has_vc = true;
			}

			triangle[i] = vertex_index;

			//CE can only handle per vertex mapping. We duplicate vertices that have multiple mappings
			if (!mapped.insert(vertex_index).second)
			{
				bool remap_normal = false;
				bool remap_uv = false;
				bool remap_vc = false;
				if (has_normal && !(nif_n == normals[vertex_index]))
				{
					remap_normal = true;
				}
				if (has_uv && !(nif_uv == uvs[vertex_index]))
				{
					remap_uv = true;
				}
				if (has_vc && !(nif_color == vcs[vertex_index]))
				{
					remap_vc = true;
				}
				if (remap_normal || remap_uv || remap_vc) {
					triangle[i] = verts.size();
					verts.push_back(verts[vertex_index]);
					if (has_normal)
						normals.push_back(nif_n);
					if (has_uv)
						uvs.push_back(nif_uv);
					if (has_vc)
						vcs.push_back(nif_color);
				}
			}
			else {
				if (has_normal)
					normals[vertex_index] = nif_n;
				if (has_uv)
					uvs[vertex_index] = nif_uv;
				if (has_vc)
					vcs[vertex_index] = nif_color;
			}
		}

		tris.emplace_back(triangle[0], triangle[1], triangle[2]);
	}
	if (verts.size() > 0) {
		data->SetHasVertices(true);
		data->SetVertices(verts);
	}

	if (tris.size()) {
		data->SetHasTriangles(true);
		data->SetNumTriangles(tris.size());
		data->SetNumTrianglePoints(tris.size() * 3);
		data->SetTriangles(tris);

		if (normals.empty() && verts.size())
		{
			Vector3 COM;
			CalculateNormals(verts, tris, normals, COM);
		}
	}

	if (normals.size() > 0) {
		data->SetHasNormals(true);
		data->SetNormals(normals);
	}

	if (vcs.size())
	{
		data->SetHasVertexColors(true);
		data->SetVertexColors(vcs);
	}

	if (m->GetDeformerCount(FbxDeformer::eSkin) > 0) {

		skins_Map[m] = out;
	}
	else {
		meshes.insert(m);
	}

	if (options.InvertU)
		for (auto &u : uvs)
			u.u = 1.0f - u.u;

	if (options.InvertV)
		for (auto &v : uvs)
			v.v = 1.0f - v.v;

	if (uvs.size() > 0) {
		data->SetHasUv(true);
		data->SetBsVectorFlags((BSVectorFlags)(data->GetBsVectorFlags() | BSVF_HAS_UV));
		data->SetUvSets({ uvs });
	}

	data->SetConsistencyFlags(CT_STATIC);

	if (verts.size() != 0 && tris.size() != 0 && uvs.size() != 0) {
		Vector3 COM;
		TriGeometryContext g(verts, COM, tris, uvs, normals);
		data->SetHasNormals(1);
		//recalculate
		data->SetNormals(g.normals);
		data->SetTangents(g.tangents);
		data->SetBitangents(g.bitangents);
		if (verts.size() != g.normals.size() || verts.size() != g.tangents.size() || verts.size() != g.bitangents.size())
			throw runtime_error("Geometry mismatch!");
		data->SetBsVectorFlags(static_cast<BSVectorFlags>(data->GetBsVectorFlags() | BSVF_HAS_TANGENTS));
	}

	BSLightingShaderProperty* shader = new BSLightingShaderProperty();

	if (data->GetHasVertexColors() == false)
		shader->SetShaderFlags2_sk(SkyrimShaderPropertyFlags2(shader->GetShaderFlags2() & ~SLSF2_VERTEX_COLORS));

	if (m->GetDeformerCount(FbxDeformer::eSkin) > 0) {
		shader->SetShaderFlags1_sk(SkyrimShaderPropertyFlags1(shader->GetShaderFlags1_sk() | SLSF1_SKINNED));
	}


	BSShaderTextureSetRef textures = new BSShaderTextureSet();
	textures->SetTextures({
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		"",
		""
		});



	if (node->GetNodeCount() > 0)
	{
		if (node->GetNodeCount() > 1)
			Log::Info("node->NodeCount() is above 1. Is this normal?");

		FbxNode* tempN = node->GetNode();
		vector<string> vTextures = textures->GetTextures();
		FbxProperty prop = nullptr;

		prop = tempN->GetFirstProperty();
		while (prop.IsValid())
		{
			string name = prop.GetNameAsCStr();
			string slot_prefix = "slot";
			if (prop.GetFlag(FbxPropertyFlags::eUserDefined))
			{
				auto it = search(
					name.begin(), name.end(),
					slot_prefix.begin(), slot_prefix.end(),
					[](char ch1, char ch2) { return toupper(ch1) == toupper(ch2); }
				);
				if (it != name.end())
				{
					name.erase(name.begin(), name.begin() + 4);
					int slot = atoi(name.c_str());
					if (slot > 2 && slot < 9)
					{
						FbxString path = prop.Get<FbxString>();
						string tempString = path.Buffer();
						size_t idx = tempString.find("textures", 0);
						if (idx != string::npos)
						{
							tempString.erase(tempString.begin(), tempString.begin() + idx);
						}
						else {
							idx = tempString.find("cube", 0);
							if (idx != string::npos)
							{
								tempString.erase(tempString.begin(), tempString.begin() + idx);
							}
						}
						vTextures[slot-1] = tempString;
					}
				}
			}
			prop = tempN->GetNextProperty(prop);
		}

		FbxSurfaceMaterial * material = tempN->GetMaterial(0);
		if (material != NULL)
		{
			FbxPropertyT<FbxDouble3> colour;
			FbxPropertyT<FbxDouble> factor;
			FbxFileTexture *texture;

			//diffuse:
			prop = material->FindProperty(FbxSurfaceMaterial::sDiffuse, true);
			if (prop.IsValid())
			{
				texture = prop.GetSrcObject<FbxFileTexture>(0);
				if (texture)
				{
					std::string tempString = string(texture->GetFileName());
					size_t idx = tempString.find("textures", 0);
					if (idx != string::npos)
					{
						tempString.erase(tempString.begin(), tempString.begin() + idx);
					}
					else {
						Log::Warn("Unable to find a relative path to texture, using full FBX path: %s", tempString.c_str());
					}
					vTextures[0] = tempString;
					if (texture->Alpha > 0.0)
						hasAlpha = true;
					if (texture->GetBlendMode() != FbxFileTexture::EBlendMode::eOver)
						hasAlpha = true;
				}

			}

			//normal:
			prop = material->FindProperty(FbxSurfaceMaterial::sBump, true);

			if (prop.IsValid())
			{
				texture = prop.GetSrcObject<FbxFileTexture>(0);

				if (texture)
				{
					std::string tempString = string(texture->GetFileName());
					size_t idx = tempString.find_first_of("textures", 0);
					tempString.erase(tempString.begin(), tempString.begin() + idx);
					vTextures[1] = tempString;
				}
				else {
					prop = material->FindProperty(FbxSurfaceMaterial::sNormalMap, true);
					if (prop.IsValid())
					{
						texture = prop.GetSrcObject<FbxFileTexture>(0);
						if (texture)
						{
							std::string tempString = string(texture->GetFileName());
							size_t idx = tempString.find_first_of("textures", 0);
							tempString.erase(tempString.begin(), tempString.begin() + idx);
							vTextures[1] = tempString;
						}
					}
				}
			}

			//Specular
			prop = material->FindProperty("Roughness", true);
			if (prop.IsValid())
			{
				texture = prop.GetSrcObject<FbxFileTexture>(0);
				if (texture)
				{
					std::string tempString = string(texture->GetFileName());
					size_t idx = tempString.find_first_of("textures", 0);
					tempString.erase(tempString.begin(), tempString.begin() + idx);
					vTextures[1] = tempString;
				}
			}
			//if this isn't found, then we could go down the alternate route and do 1f-transparency?
			factor = material->FindProperty("Opacity", true);

			if (factor.IsValid())
			{
				printf("%f", factor.Get());
				shader->SetAlpha(factor.Get());
			}

			//spec:
			colour = material->FindProperty(material->sSpecular, true);
			factor = material->FindProperty(material->sSpecularFactor, true);
			if (colour.IsValid() && factor.IsValid())
			{
				//correct set this flag or my ocd will throw fits.
				factor.Get() > 0.0 ? shader->SetShaderFlags1_sk(SkyrimShaderPropertyFlags1(shader->GetShaderFlags1_sk() | SLSF1_SPECULAR)) : shader->SetShaderFlags1_sk(SkyrimShaderPropertyFlags1(shader->GetShaderFlags1_sk() & ~SLSF1_SPECULAR));
				FbxDouble3 colourvec = colour.Get();
				shader->SetSpecularStrength(factor.Get());
				shader->SetSpecularColor(Color3(colourvec[0], colourvec[1], colourvec[2]));
			}

			//emissive
			colour = material->FindProperty(material->sEmissive, true);
			factor = material->FindProperty(material->sEmissiveFactor, true);
			if (colour.IsValid() && factor.IsValid())
			{
				FbxDouble3 colourvec = colour.Get();
				shader->SetEmissiveMultiple(factor.Get());
				shader->SetEmissiveColor(Color3(colourvec[0], colourvec[1], colourvec[2]));
			}

			//	//shiny/gloss
			factor = material->FindProperty(material->sShininess, true);
			if (factor.IsValid())
			{
				shader->SetGlossiness(factor.Get());
			}

			textures->SetTextures(vTextures);
		}
	}

	if (hasAlpha) {
		NiAlphaPropertyRef alpharef = new NiAlphaProperty();
		alpharef->SetFlags(237);
		alpharef->SetThreshold(128);
		out->SetAlphaProperty(alpharef);
	}

	shader->SetTextureSet(textures);

	out->SetFlags(524302);
	out->SetShaderProperty(shader);
	out->SetData(StaticCast<NiGeometryData>(data));


	return out;
}

NiNodeRef FBXWrangler::importShapes(FbxNode* child, const FBXImportOptions& options) {
	NiNodeRef dummy = new NiNode();
	string name = child->GetName();
	dummy->SetName(unsanitizeString(name));
	vector<NiAVObjectRef> children;
	size_t attributes_size = child->GetNodeAttributeCount();
	for (int i = 0; i < attributes_size; i++) {
		if (FbxNodeAttribute::eMesh == child->GetNodeAttributeByIndex(i)->GetAttributeType())
		{		
			string sub_name = name + "_mesh_" + to_string(i);
			children.push_back(StaticCast<NiAVObject>(importShape(sub_name, child->GetNodeAttributeByIndex(i), options)));
		}
	}
	dummy->SetChildren(children);
	return dummy;
}

KeyType collect_times(FbxAnimCurve* curveX, set<double>& times, KeyType fixed_type)
{
	KeyType type = CONST_KEY;
	if (curveX != NULL)
	{
		for (int i = 0; i < curveX->KeyGetCount(); i++)
		{
			FbxAnimCurveKey& key = curveX->KeyGet(i);
			times.insert(key.GetTime().GetSecondDouble());
			KeyType new_type = CONST_KEY;
			switch (key.GetInterpolation())
			{
			case FbxAnimCurveDef::EInterpolationType::eInterpolationConstant:
				break;
			case FbxAnimCurveDef::EInterpolationType::eInterpolationLinear:
				new_type = LINEAR_KEY;
			case FbxAnimCurveDef::EInterpolationType::eInterpolationCubic:
				new_type = QUADRATIC_KEY;
			}
			if (i > 0 && type != new_type)
			{
				Log::Warn("Found an FbxAnimCurve with mixed types of interpolation, NIF doesn't support that for translations!");
			}
			type = new_type;
		}
	}
	return type;
}

void addTranslationKeys(NiTransformInterpolator* interpolator, FbxNode* node, FbxAnimCurve* curveX, FbxAnimCurve* curveY, FbxAnimCurve* curveZ, double time_offset) {
	map<double, int> timeMapX;
	map<double, int> timeMapY;
	map<double, int> timeMapZ;

	FbxDouble3 position = node->LclTranslation.Get();

	KeyType interp = CONST_KEY;
	set<double> times;
	interp = collect_times(curveX, times, interp);
	interp = collect_times(curveY, times, interp);
	interp = collect_times(curveZ, times, interp);

	if (times.size() > 0)
	{
		times.insert(time_offset);

		NiTransformDataRef data = interpolator->GetData();
		if (data == NULL) data = new NiTransformData();
		KeyGroup<Vector3 > tkeys = data->GetTranslations();
		vector<Key<Vector3 > > keyvalues = tkeys.keys;
		for (const auto& time : times) {
			FbxTime lTime;

			// Set the time at two seconds.
			lTime.SetSecondDouble((float)time);
			FbxVector4 trans = node->EvaluateLocalTransform(lTime).GetT();

			Key<Vector3 > temp;
			temp.data = Vector3(trans[0], trans[1], trans[2]);
			if (interp == QUADRATIC_KEY)
			{
				temp.forward_tangent = {
					curveX != NULL ? curveX->EvaluateRightDerivative(lTime) : 0.0f,
					curveY != NULL ? curveY->EvaluateRightDerivative(lTime) : 0.0f,
					curveZ != NULL ? curveZ->EvaluateRightDerivative(lTime) : 0.0f,
				};
				temp.backward_tangent = {
					curveX != NULL ? curveX->EvaluateLeftDerivative(lTime) : 0.0f,
					curveY != NULL ? curveY->EvaluateLeftDerivative(lTime) : 0.0f,
					curveZ != NULL ? curveZ->EvaluateLeftDerivative(lTime) : 0.0f,
				};

			}
			temp.time = time - time_offset;
			keyvalues.push_back(temp);
		}
		tkeys.numKeys = keyvalues.size();
		tkeys.keys = keyvalues;
		tkeys.interpolation = interp;
		data->SetTranslations(tkeys);
		interpolator->SetData(data);
	}
}

template<>
class Accessor<NiTransformData> {
public:
	Accessor(NiTransformDataRef ref) {
		ref->numRotationKeys = 1;
	}
};

int pack_float_key(FbxAnimCurve* curveI, KeyGroup<float>& keys, float time_offset, bool deg_to_rad)
{
	int IkeySize = 0;
	bool has_key_in_time_offset = false;
	if (curveI != NULL)
	{
		IkeySize = curveI->KeyGetCount();
		if (IkeySize > 0) {
			//KeyGroup<float>& keys = tkeys[0];
			for (int i = 0; i < IkeySize; i++) {
				FbxAnimCurveKey fbx_key = curveI->KeyGet(i);
				Key<float> new_key;
				if (fbx_key.GetTime().GetSecondDouble() == time_offset)
					has_key_in_time_offset = true;
				new_key.time = fbx_key.GetTime().GetSecondDouble() - time_offset;
				new_key.data = fbx_key.GetValue();
				new_key.forward_tangent = curveI->KeyGetRightTangentVelocity(i);
				new_key.backward_tangent = curveI->KeyGetLeftTangentVelocity(i);
				if (deg_to_rad)
				{
					new_key.data = deg2rad(new_key.data);
					new_key.forward_tangent = deg2rad(new_key.forward_tangent);
					new_key.backward_tangent = deg2rad(new_key.backward_tangent);
				}
				keys.keys.push_back(new_key);
			}
			if (!has_key_in_time_offset)
			{
				IkeySize += 1;
				Key<float> new_key;
				new_key.time = time_offset;
				FbxTime ttime; ttime.SetSecondDouble(time_offset);
				new_key.data = curveI->Evaluate(time_offset);
				new_key.forward_tangent = curveI->EvaluateRightDerivative(ttime);
				new_key.backward_tangent = curveI->EvaluateLeftDerivative(ttime);
				if (deg_to_rad)
				{
					new_key.data = deg2rad(new_key.data);
					new_key.forward_tangent = deg2rad(new_key.forward_tangent);
					new_key.backward_tangent = deg2rad(new_key.backward_tangent);
				}
				keys.keys.insert(keys.keys.begin(), new_key);
			}

			keys.numKeys = keys.keys.size();
			keys.interpolation = QUADRATIC_KEY;

		}
	}
	return IkeySize;
}


void addRotationKeys(NiTransformInterpolator* interpolator, FbxNode* node, FbxAnimCurve* curveI, FbxAnimCurve* curveJ, FbxAnimCurve* curveK, double time_offset) {
	//this is simpler because curves can be evaluated one at a time
	NiTransformDataRef data = interpolator->GetData();
	if (data == NULL) data = new NiTransformData();
	Niflib::array<3, KeyGroup<float > > tkeys = data->GetXyzRotations();

	int IkeySize = pack_float_key(curveI, tkeys[0], time_offset, true);
	int JkeySize = pack_float_key(curveI, tkeys[1], time_offset, true);
	int KkeySize = pack_float_key(curveI, tkeys[2], time_offset, true);

	if (IkeySize > 0 || JkeySize > 0 || KkeySize > 0) {
		Accessor<NiTransformData> fix_rot(data);
		data->SetXyzRotations(tkeys);
		data->SetRotationType(XYZ_ROTATION_KEY);
		interpolator->SetData(data);
	}
}

void addScaleKeys(NiTransformInterpolator* interpolator, FbxNode* node, FbxAnimCurve* curveI, FbxAnimCurve* curveJ, FbxAnimCurve* curveK, double time_offset) {
	//this is simpler because curves can be evaluated one at a time
	NiTransformDataRef data = interpolator->GetData();
	if (data == NULL) data = new NiTransformData();
	KeyGroup<float >  tkeys = data->GetScales();

	int IkeySize = pack_float_key(curveI, tkeys, time_offset, false);
	if (IkeySize > 0) {
		Accessor<NiTransformData> fix_rot(data);
		data->SetScales(tkeys);
		interpolator->SetData(data);
	}
}

double FBXWrangler::convert(FbxAnimLayer* pAnimLayer, NiControllerSequenceRef sequence, set<NiObjectRef>& targets, NiControllerManagerRef manager, NiMultiTargetTransformControllerRef multiController, string accum_root_name, double last_start)
{
	// Display general curves.
	vector<ControlledBlock > blocks = sequence->GetControlledBlocks();
	for (FbxNode* pNode : unskinned_bones)
	{
		//FbxNode* pNode = pair.first;
		FbxAnimCurve* lXAnimCurve = pNode->LclTranslation.GetCurve(pAnimLayer, FBXSDK_CURVENODE_COMPONENT_X);
		FbxAnimCurve* lYAnimCurve = pNode->LclTranslation.GetCurve(pAnimLayer, FBXSDK_CURVENODE_COMPONENT_Y);
		FbxAnimCurve* lZAnimCurve = pNode->LclTranslation.GetCurve(pAnimLayer, FBXSDK_CURVENODE_COMPONENT_Z);
		FbxAnimCurve* lIAnimCurve = pNode->LclRotation.GetCurve(pAnimLayer, FBXSDK_CURVENODE_COMPONENT_X);
		FbxAnimCurve* lJAnimCurve = pNode->LclRotation.GetCurve(pAnimLayer, FBXSDK_CURVENODE_COMPONENT_Y);
		FbxAnimCurve* lKAnimCurve = pNode->LclRotation.GetCurve(pAnimLayer, FBXSDK_CURVENODE_COMPONENT_Z);
		FbxAnimCurve* lsXAnimCurve = pNode->LclScaling.GetCurve(pAnimLayer, FBXSDK_CURVENODE_COMPONENT_X);
		FbxAnimCurve* lsYAnimCurve = pNode->LclScaling.GetCurve(pAnimLayer, FBXSDK_CURVENODE_COMPONENT_Y);
		FbxAnimCurve* lsZAnimCurve = pNode->LclScaling.GetCurve(pAnimLayer, FBXSDK_CURVENODE_COMPONENT_Z);

		if (lXAnimCurve != NULL || lYAnimCurve != NULL || lZAnimCurve != NULL ||
			lIAnimCurve != NULL || lJAnimCurve != NULL || lKAnimCurve != NULL ||
			lsXAnimCurve != NULL || lsYAnimCurve != NULL || lsZAnimCurve != NULL)
		{

			NiObjectRef target = conversion_Map[pNode];
			targets.insert(target);

			NiTransformInterpolatorRef interpolator = new NiTransformInterpolator();
			NiQuatTransform trans;
			unsigned int float_min = 0xFF7FFFFF;
			float* lol = (float*)&float_min;
			trans.translation = Vector3(*lol, *lol, *lol);
			trans.rotation = Quaternion(*lol, *lol, *lol, *lol);
			trans.scale = *lol;
			interpolator->SetTransform(trans);

			if (lXAnimCurve != NULL || lYAnimCurve != NULL || lZAnimCurve != NULL) {
				addTranslationKeys(interpolator, pNode, lXAnimCurve, lYAnimCurve, lZAnimCurve, last_start);
			}

			if (lIAnimCurve != NULL || lJAnimCurve != NULL || lKAnimCurve != NULL) {
				addRotationKeys(interpolator, pNode, lIAnimCurve, lJAnimCurve, lKAnimCurve, last_start);
			}

			if (lsXAnimCurve != NULL ) {
				addScaleKeys(interpolator, pNode, lsXAnimCurve, lsYAnimCurve, lsZAnimCurve, last_start);
			}

			ControlledBlock block;
			block.interpolator = interpolator;
			block.nodeName = DynamicCast<NiAVObject>(conversion_Map[pNode])->GetName();
			block.controllerType = "NiTransformController";
			block.controller = multiController;
		
			blocks.push_back(block);

			vector<FbxTimeSpan> spans(9);
		
			if (lXAnimCurve != NULL)
				lXAnimCurve->GetTimeInterval(spans[0]);
			if (lYAnimCurve != NULL)
				lYAnimCurve->GetTimeInterval(spans[1]);
			if (lZAnimCurve != NULL)
				lZAnimCurve->GetTimeInterval(spans[2]);
			if (lIAnimCurve != NULL)
				lIAnimCurve->GetTimeInterval(spans[3]);
			if (lJAnimCurve != NULL)
				lJAnimCurve->GetTimeInterval(spans[4]);
			if (lKAnimCurve != NULL)
				lKAnimCurve->GetTimeInterval(spans[5]);
			if (lsXAnimCurve != NULL)
				lsXAnimCurve->GetTimeInterval(spans[6]);
			if (lsYAnimCurve != NULL)
				lsYAnimCurve->GetTimeInterval(spans[7]);
			if (lsZAnimCurve != NULL)
				lsZAnimCurve->GetTimeInterval(spans[8]);
			
			double start = 1e10;
			double end = -1e10;

			for (const auto& span : spans) {
				double span_start = span.GetStart().GetSecondDouble();
				double span_stop = span.GetStop().GetSecondDouble();
				if (span_start < start)
					start = span_start;
				if (span_stop > end)
					end = span_stop;
			}

			sequence->SetControlledBlocks(blocks);

			sequence->SetStartTime(0.0);
			sequence->SetStopTime(end-start);
			sequence->SetManager(manager);
			sequence->SetAccumRootName(accum_root_name);

			NiTextKeyExtraDataRef extra_data = new NiTextKeyExtraData();
			extra_data->SetName(string(""));
			Key<IndexString> start_key;
			start_key.time = 0;
			start_key.data = "start";

			Key<IndexString> end_key;
			end_key.time = end - last_start;
			end_key.data = "end";

			extra_data->SetTextKeys({ start_key,end_key });
			extra_data->SetNextExtraData(NULL);

			sequence->SetTextKeys(extra_data);

			sequence->SetFrequency(1.0);
			sequence->SetCycleType(CYCLE_CLAMP);

		}

		

		//cannot support the rest right now
#if 0
	//	if (lAnimCurve)
	//	{
	//		FBXSDK_printf("        TX\n");
	//	}
	//	
	//	if (lAnimCurve)
	//	{
	//		FBXSDK_printf("        TY\n");
	//	}
	//	
	//	if (lAnimCurve)
	//	{
	//		FBXSDK_printf("        TZ\n");
	//	}
	//	
	//	if (lAnimCurve)
	//	{
	//		FBXSDK_printf("        RX\n");
	//	}
	//	
	//	if (lAnimCurve)
	//	{
	//		FBXSDK_printf("        RY\n");
	//	}
	//	lAnimCurve = ;
	//	if (lAnimCurve)
	//	{
	//		FBXSDK_printf("        RZ\n");
	//	}
	//	lAnimCurve = pNode->LclScaling.GetCurve(pAnimLayer, FBXSDK_CURVENODE_COMPONENT_X);
	//	if (lAnimCurve)
	//	{
	//		FBXSDK_printf("        SX\n");
	//	}
	//	lAnimCurve = pNode->LclScaling.GetCurve(pAnimLayer, FBXSDK_CURVENODE_COMPONENT_Y);
	//	if (lAnimCurve)
	//	{
	//		FBXSDK_printf("        SY\n");
	//	}
	//	lAnimCurve = pNode->LclScaling.GetCurve(pAnimLayer, FBXSDK_CURVENODE_COMPONENT_Z);
	//	if (lAnimCurve)
	//	{
	//		FBXSDK_printf("        SZ\n");
	//	}
	//
	//// Display curves specific to a light or marker.
	//FbxNodeAttribute* lNodeAttribute = pNode->GetNodeAttribute();
	//if (lNodeAttribute)
	//{
	//	lAnimCurve = lNodeAttribute->Color.GetCurve(pAnimLayer, FBXSDK_CURVENODE_COLOR_RED);
	//	if (lAnimCurve)
	//	{
	//		FBXSDK_printf("        Red\n");
	//	}
	//	lAnimCurve = lNodeAttribute->Color.GetCurve(pAnimLayer, FBXSDK_CURVENODE_COLOR_GREEN);
	//	if (lAnimCurve)
	//	{
	//		FBXSDK_printf("        Green\n");
	//	}
	//	lAnimCurve = lNodeAttribute->Color.GetCurve(pAnimLayer, FBXSDK_CURVENODE_COLOR_BLUE);
	//	if (lAnimCurve)
	//	{
	//		FBXSDK_printf("        Blue\n");
	//	}
	//	// Display curves specific to a light.
	//	FbxLight* light = pNode->GetLight();
	//	if (light)
	//	{
	//		lAnimCurve = light->Intensity.GetCurve(pAnimLayer);
	//		if (lAnimCurve)
	//		{
	//			FBXSDK_printf("        Intensity\n");
	//		}
	//		lAnimCurve = light->OuterAngle.GetCurve(pAnimLayer);
	//		if (lAnimCurve)
	//		{
	//			FBXSDK_printf("        Outer Angle\n");
	//		}
	//		lAnimCurve = light->Fog.GetCurve(pAnimLayer);
	//		if (lAnimCurve)
	//		{
	//			FBXSDK_printf("        Fog\n");
	//		}
	//	}
	//	// Display curves specific to a camera.
	//	FbxCamera* camera = pNode->GetCamera();
	//	if (camera)
	//	{
	//		lAnimCurve = camera->FieldOfView.GetCurve(pAnimLayer);
	//		if (lAnimCurve)
	//		{
	//			FBXSDK_printf("        Field of View\n");
	//		}
	//		lAnimCurve = camera->FieldOfViewX.GetCurve(pAnimLayer);
	//		if (lAnimCurve)
	//		{
	//			FBXSDK_printf("        Field of View X\n");
	//		}
	//		lAnimCurve = camera->FieldOfViewY.GetCurve(pAnimLayer);
	//		if (lAnimCurve)
	//		{
	//			FBXSDK_printf("        Field of View Y\n");
	//		}
	//		lAnimCurve = camera->OpticalCenterX.GetCurve(pAnimLayer);
	//		if (lAnimCurve)
	//		{
	//			FBXSDK_printf("        Optical Center X\n");
	//		}
	//		lAnimCurve = camera->OpticalCenterY.GetCurve(pAnimLayer);
	//		if (lAnimCurve)
	//		{
	//			FBXSDK_printf("        Optical Center Y\n");
	//		}
	//		lAnimCurve = camera->Roll.GetCurve(pAnimLayer);
	//		if (lAnimCurve)
	//		{
	//			FBXSDK_printf("        Roll\n");
	//		}
	//	}
	//	// Display curves specific to a geometry.
	//	if (lNodeAttribute->GetAttributeType() == FbxNodeAttribute::eMesh ||
	//		lNodeAttribute->GetAttributeType() == FbxNodeAttribute::eNurbs ||
	//		lNodeAttribute->GetAttributeType() == FbxNodeAttribute::ePatch)
	//	{
	//		FbxGeometry* lGeometry = (FbxGeometry*)lNodeAttribute;
	//		int lBlendShapeDeformerCount = lGeometry->GetDeformerCount(FbxDeformer::eBlendShape);
	//		for (int lBlendShapeIndex = 0; lBlendShapeIndex<lBlendShapeDeformerCount; ++lBlendShapeIndex)
	//		{
	//			FbxBlendShape* lBlendShape = (FbxBlendShape*)lGeometry->GetDeformer(lBlendShapeIndex, FbxDeformer::eBlendShape);
	//			int lBlendShapeChannelCount = lBlendShape->GetBlendShapeChannelCount();
	//			for (int lChannelIndex = 0; lChannelIndex<lBlendShapeChannelCount; ++lChannelIndex)
	//			{
	//				FbxBlendShapeChannel* lChannel = lBlendShape->GetBlendShapeChannel(lChannelIndex);
	//				const char* lChannelName = lChannel->GetName();
	//				lAnimCurve = lGeometry->GetShapeChannel(lBlendShapeIndex, lChannelIndex, pAnimLayer, true);
	//				if (lAnimCurve)
	//				{
	//					FBXSDK_printf("        Shape %s\n", lChannelName);
	//				}
	//			}
	//		}
	//	}
	//}
	//// Display curves specific to properties
	//FbxProperty lProperty = pNode->GetFirstProperty();
	//while (lProperty.IsValid())
	//{
	//	if (lProperty.GetFlag(FbxPropertyFlags::eUserDefined))
	//	{
	//		FbxString lFbxFCurveNodeName = lProperty.GetName();
	//		FbxAnimCurveNode* lCurveNode = lProperty.GetCurveNode(pAnimLayer);
	//		if (!lCurveNode) {
	//			lProperty = pNode->GetNextProperty(lProperty);
	//			continue;
	//		}
	//		FbxDataType lDataType = lProperty.GetPropertyDataType();
	//		if (lDataType.GetType() == eFbxBool || lDataType.GetType() == eFbxDouble || lDataType.GetType() == eFbxFloat || lDataType.GetType() == eFbxInt)
	//		{
	//			FbxString lMessage;
	//			lMessage = "        Property ";
	//			lMessage += lProperty.GetName();
	//			if (lProperty.GetLabel().GetLen() > 0)
	//			{
	//				lMessage += " (Label: ";
	//				lMessage += lProperty.GetLabel();
	//				lMessage += ")";
	//			};
	//			FBXSDK_printf(lMessage.Buffer());
	//			for (int c = 0; c < lCurveNode->GetCurveCount(0U); c++)
	//			{
	//				lAnimCurve = lCurveNode->GetCurve(0U, c);
	//				//if (lAnimCurve)
	//				//	DisplayCurve(lAnimCurve);
	//			}
	//		}
	//		else if (lDataType.GetType() == eFbxDouble3 || lDataType.GetType() == eFbxDouble4 || lDataType.Is(FbxColor3DT) || lDataType.Is(FbxColor4DT))
	//		{
	//			char* lComponentName1 = (lDataType.Is(FbxColor3DT) || lDataType.Is(FbxColor4DT)) ? (char*)FBXSDK_CURVENODE_COLOR_RED : (char*)"X";
	//			char* lComponentName2 = (lDataType.Is(FbxColor3DT) || lDataType.Is(FbxColor4DT)) ? (char*)FBXSDK_CURVENODE_COLOR_GREEN : (char*)"Y";
	//			char* lComponentName3 = (lDataType.Is(FbxColor3DT) || lDataType.Is(FbxColor4DT)) ? (char*)FBXSDK_CURVENODE_COLOR_BLUE : (char*)"Z";
	//			FbxString      lMessage;

	//			lMessage = "        Property ";
	//			lMessage += lProperty.GetName();
	//			if (lProperty.GetLabel().GetLen() > 0)
	//			{
	//				lMessage += " (Label: ";
	//				lMessage += lProperty.GetLabel();
	//				lMessage += ")";
	//			}
	//			FBXSDK_printf(lMessage.Buffer());
	//			for (int c = 0; c < lCurveNode->GetCurveCount(0U); c++)
	//			{
	//				lAnimCurve = lCurveNode->GetCurve(0U, c);
	//				if (lAnimCurve)
	//				{
	//					FBXSDK_printf("        Component ", lComponentName1);
	//				}
	//			}
	//			for (int c = 0; c < lCurveNode->GetCurveCount(1U); c++)
	//			{
	//				lAnimCurve = lCurveNode->GetCurve(1U, c);
	//				if (lAnimCurve)
	//				{
	//					FBXSDK_printf("        Component ", lComponentName2);
	//				}
	//			}
	//			for (int c = 0; c < lCurveNode->GetCurveCount(2U); c++)
	//			{
	//				lAnimCurve = lCurveNode->GetCurve(2U, c);
	//				if (lAnimCurve)
	//				{
	//					FBXSDK_printf("        Component ", lComponentName3);
	//				}
	//			}
	//		}
	//		else if (lDataType.GetType() == eFbxEnum)
	//		{
	//			FbxString lMessage;
	//			lMessage = "        Property ";
	//			lMessage += lProperty.GetName();
	//			if (lProperty.GetLabel().GetLen() > 0)
	//			{
	//				lMessage += " (Label: ";
	//				lMessage += lProperty.GetLabel();
	//				lMessage += ")";
	//			};
	//			FBXSDK_printf(lMessage.Buffer());
	//			for (int c = 0; c < lCurveNode->GetCurveCount(0U); c++)
	//			{
	//				lAnimCurve = lCurveNode->GetCurve(0U, c);
	//				//if (lAnimCurve)
	//				//	DisplayListCurve(lAnimCurve, &lProperty);
	//			}
	//		}
	//	}
	//	lProperty = pNode->GetNextProperty(lProperty);
	//} // while
#endif
	}
	return 0.0;
}

//build embedded KF animations, for anything unskinned
void FBXWrangler::buildKF() {
	//create a controller manager
	NiControllerManagerRef controller = new NiControllerManager();
	vector<Ref<NiControllerSequence > > sequences = controller->GetControllerSequences();
	NiMultiTargetTransformControllerRef transformController = new NiMultiTargetTransformController();
	set<NiObjectRef> extra_targets;
	for (int i = 0; i < unskinned_animations.size(); i++)
	{
		FbxAnimStack* lAnimStack = scene->GetSrcObject<FbxAnimStack>(i);
		//could contain more than a layer, but by convention we wean just the first
		FbxAnimLayer* lAnimLayer = lAnimStack->GetMember<FbxAnimLayer>(0);
		FbxTimeSpan reference = lAnimStack->GetReferenceTimeSpan();
		FbxTimeSpan local = lAnimStack->GetLocalTimeSpan();
		NiControllerSequenceRef sequence = new NiControllerSequence();
		//Translate
		convert(lAnimLayer, sequence, extra_targets, controller, transformController, string(conversion_root->GetName()), reference.GetStart().GetSecondDouble());

		sequence->SetName(string(lAnimStack->GetName()));
		sequences_names.insert(string(lAnimStack->GetName()));
		sequences.push_back(sequence);
	}

	vector<NiAVObject * > etargets = transformController->GetExtraTargets();
	NiDefaultAVObjectPaletteRef palette = new NiDefaultAVObjectPalette();
	vector<AVObject > avobjs = palette->GetObjs();
	for (const auto& target : extra_targets) {
		etargets.push_back(DynamicCast<NiAVObject>(target));
		AVObject avobj;
		avobj.avObject = DynamicCast<NiAVObject>(target);
		avobj.name = DynamicCast<NiAVObject>(target)->GetName();
		avobjs.push_back(avobj);
	}
	AVObject avobj;
	avobj.avObject = StaticCast<NiAVObject>(conversion_root);
	avobj.name = StaticCast<NiAVObject>(conversion_root)->GetName();
	avobjs.push_back(avobj);

	transformController->SetFlags(44);
	transformController->SetFrequency(1.0f);
	transformController->SetPhase(0.0f);
	transformController->SetExtraTargets(etargets);
	transformController->SetTarget(conversion_root);
	//transformController->SetStartTime(0x7f7fffff);
	//transformController->SetStopTime(0xff7fffff);


	palette->SetScene(StaticCast<NiAVObject>(conversion_root));
	palette->SetObjs(avobjs);

	controller->SetObjectPalette(palette);
	controller->SetControllerSequences(sequences);
	controller->SetNextController(StaticCast<NiTimeController>(transformController));

	controller->SetFlags(12);
	controller->SetFrequency(1.0f);
	controller->SetPhase(0.0f);
	//controller->SetStartTime(0x7f7fffff);
	//controller->SetStopTime(0xff7fffff);

	controller->SetTarget(conversion_root);
	conversion_root->SetController(StaticCast<NiTimeController>(controller));
}

void FBXWrangler::checkAnimatedNodes()
{
	size_t stacks = scene->GetSrcObjectCount<FbxAnimStack>();
	for (int i = 0; i < stacks; i++)
	{
		FbxAnimStack* lAnimStack = scene->GetSrcObject<FbxAnimStack>(i);
		//could contain more than a layer, but by convention we use just the first, assuming the animation has been baked
		FbxAnimLayer* pAnimLayer = lAnimStack->GetMember<FbxAnimLayer>(0);
		//for safety, we only check analyzed nodes
		for (const auto& pair : conversion_Map)
		{
			FbxNode* pNode = pair.first;

			bool isAnimated = false;
			bool hasFloatTracks = false;
			bool hasSkinnedTracks = false;
			bool isAnnotated = false;

			vector<FbxAnimCurve*> movements_curves;

			movements_curves.push_back(pNode->LclTranslation.GetCurve(pAnimLayer, FBXSDK_CURVENODE_COMPONENT_X));
			movements_curves.push_back(pNode->LclTranslation.GetCurve(pAnimLayer, FBXSDK_CURVENODE_COMPONENT_Y));
			movements_curves.push_back(pNode->LclTranslation.GetCurve(pAnimLayer, FBXSDK_CURVENODE_COMPONENT_Z));
			movements_curves.push_back(pNode->LclRotation.GetCurve(pAnimLayer, FBXSDK_CURVENODE_COMPONENT_X));
			movements_curves.push_back(pNode->LclRotation.GetCurve(pAnimLayer, FBXSDK_CURVENODE_COMPONENT_Y));
			movements_curves.push_back(pNode->LclRotation.GetCurve(pAnimLayer, FBXSDK_CURVENODE_COMPONENT_Z));
			movements_curves.push_back(pNode->LclScaling.GetCurve(pAnimLayer, FBXSDK_CURVENODE_COMPONENT_X));
			movements_curves.push_back(pNode->LclScaling.GetCurve(pAnimLayer, FBXSDK_CURVENODE_COMPONENT_Y));
			movements_curves.push_back(pNode->LclScaling.GetCurve(pAnimLayer, FBXSDK_CURVENODE_COMPONENT_Z));

			for (FbxAnimCurve* curve : movements_curves)
			{
				if (curve != NULL)
				{
					//check if the curve itself is just 0
					size_t keys = curve->KeyGetCount();
					if (keys > 0)
					{
						////has movements tracks
						isAnimated = true;
						animated_nodes.insert(pNode);
						if (skinned_bones.find(pNode) != skinned_bones.end() ||
							//it might not have the mesh not a proper eskeleton, but if we set an external one
							//check all the possible nodes for bone tracks
							!external_skeleton_path.empty())
						{
							hasSkinnedTracks = true;
						}
					}
					break;
				}
			}

			FbxProperty p = pNode->GetFirstProperty();
			
			while (p.IsValid())
			{
				string property_name = p.GetNameAsCStr();
				FbxAnimCurveNode* curveNode = p.GetCurveNode();
				if (property_name.find("hk") != string::npos)
				{
					//has annotations
					if (p.GetPropertyDataType().Is(FbxEnumDT))
					{
						annotated.insert(p);
					}
					else if (p.IsAnimated()){
						float_properties.insert(p);
					}
				}					
				p = pNode->GetNextProperty(p);
			}

			if (isAnimated) {
				if (hasSkinnedTracks)
					skinned_animations.insert(lAnimStack);
				else
				{
					unskinned_animations.insert(lAnimStack);
					unskinned_bones.insert(pNode);
				}
			}
		}
	}
}

void FBXWrangler::setExternalSkeletonPath(const string& external_skeleton_path) {
	this->external_skeleton_path = external_skeleton_path;
}

vector<size_t> getParentChain(const vector<int>& parentMap, const size_t node) {
	vector<size_t> vresult;
	int current_node = node;
	if (current_node > 0 && current_node < parentMap.size())
	{
		//his better not be a graph
		while (current_node != -1) {
			vresult.insert(vresult.begin(),current_node);
			current_node = parentMap[current_node];
		}
	}

	return move(vresult);
}

size_t getNearestCommonAncestor(const vector<int>& parentMap, const set<size_t>& nodes) {
	size_t result = NULL;
	size_t max_size_index = 0;
	size_t max_size = 0;
	int actual_index = 0;
	vector<vector<size_t>> chains;
	for (const auto& node_index : nodes)
	{
		chains.push_back(getParentChain(parentMap, node_index));
	}

	for (const auto& chain : chains) {
		if (chain.size() > max_size) {
			max_size = chain.size();
			max_size_index = actual_index;
		}
		actual_index++;
	}
	const vector<size_t>& max_chain = chains[max_size_index];
	for (const auto& bone : max_chain) {
		for (const auto& chain : chains) {
			if (find(chain.begin(), chain.end(), bone) == chain.end()) {
				return result;
			}
		}
		result = bone;
	}
	return result;
}


set<FbxNode*> FBXWrangler::buildBonesList()
{
	set<FbxNode*> bones;
	for (int meshIndex = 0; meshIndex < scene->GetGeometryCount(); ++meshIndex)
	{
		const auto mesh = static_cast<FbxMesh*>(scene->GetGeometry(meshIndex));
		if (mesh->GetDeformerCount(FbxDeformer::eSkin) > 0) {

			for (int iSkin = 0; iSkin < mesh->GetDeformerCount(FbxDeformer::eSkin); iSkin++) {
				FbxSkin* skin = (FbxSkin*)mesh->GetDeformer(iSkin, FbxDeformer::eSkin);
				for (int iCluster = 0; iCluster < skin->GetClusterCount(); iCluster++) {
					FbxCluster* cluster = skin->GetCluster(iCluster);
					if (!cluster->GetLink())
						continue;
					bones.insert(cluster->GetLink());
				}
			}
		}
	}
	return move(bones);
}

struct KeySetter {};

template<>
struct Accessor<KeySetter> {
	Accessor(bhkListShapeRef list, const vector<unsigned int>& keys)
	{
		list->unknownInts = keys;
	}
};

class BCMSPacker {};

template<>
class Accessor<BCMSPacker> {
public:
	Accessor(hkpCompressedMeshShape* pCompMesh, bhkCompressedMeshShapeDataRef pData, const vector<SkyrimHavokMaterial>& materials)
	{
		short                                   chunkIdxNif(0);

		pData->SetBoundsMin(Vector4(pCompMesh->m_bounds.m_min(0), pCompMesh->m_bounds.m_min(1), pCompMesh->m_bounds.m_min(2), pCompMesh->m_bounds.m_min(3)));
		pData->SetBoundsMax(Vector4(pCompMesh->m_bounds.m_max(0), pCompMesh->m_bounds.m_max(1), pCompMesh->m_bounds.m_max(2), pCompMesh->m_bounds.m_max(3)));

		pData->SetBitsPerIndex(pCompMesh->m_bitsPerIndex);
		pData->SetBitsPerWIndex(pCompMesh->m_bitsPerWIndex);
		pData->SetMaskIndex(pCompMesh->m_indexMask);
		pData->SetMaskWIndex(pCompMesh->m_wIndexMask);
		pData->SetWeldingType(0); //seems to be fixed for skyrim pData->SetWeldingType(pCompMesh->m_weldingType);
		pData->SetMaterialType(1); //seems to be fixed for skyrim pData->SetMaterialType(pCompMesh->m_materialType);
		pData->SetError(pCompMesh->m_error);

		//  resize and copy bigVerts
		vector<Vector4 > tVec4Vec(pCompMesh->m_bigVertices.getSize());
		for (unsigned int idx(0); idx < pCompMesh->m_bigVertices.getSize(); ++idx)
		{
			tVec4Vec[idx].x = pCompMesh->m_bigVertices[idx](0);
			tVec4Vec[idx].y = pCompMesh->m_bigVertices[idx](1);
			tVec4Vec[idx].z = pCompMesh->m_bigVertices[idx](2);
			tVec4Vec[idx].w = pCompMesh->m_bigVertices[idx](3);
		}
		pData->SetBigVerts(tVec4Vec);

		//  resize and copy bigTris
		vector<bhkCMSDBigTris > tBTriVec(pCompMesh->m_bigTriangles.getSize());
		for (unsigned int idx(0); idx < pCompMesh->m_bigTriangles.getSize(); ++idx)
		{
			tBTriVec[idx].triangle1 = pCompMesh->m_bigTriangles[idx].m_a;
			tBTriVec[idx].triangle2 = pCompMesh->m_bigTriangles[idx].m_b;
			tBTriVec[idx].triangle3 = pCompMesh->m_bigTriangles[idx].m_c;
			tBTriVec[idx].material = pCompMesh->m_bigTriangles[idx].m_material;
			tBTriVec[idx].weldingInfo = pCompMesh->m_bigTriangles[idx].m_weldingInfo;
		}
		pData->SetBigTris(tBTriVec);

		//  resize and copy transform data
		vector<bhkCMSDTransform > tTranVec(pCompMesh->m_transforms.getSize());
		for (unsigned int idx(0); idx < pCompMesh->m_transforms.getSize(); ++idx)
		{
			tTranVec[idx].translation.x = pCompMesh->m_transforms[idx].m_translation(0);
			tTranVec[idx].translation.y = pCompMesh->m_transforms[idx].m_translation(1);
			tTranVec[idx].translation.z = pCompMesh->m_transforms[idx].m_translation(2);
			tTranVec[idx].translation.w = pCompMesh->m_transforms[idx].m_translation(3);
			tTranVec[idx].rotation.x = pCompMesh->m_transforms[idx].m_rotation(0);
			tTranVec[idx].rotation.y = pCompMesh->m_transforms[idx].m_rotation(1);
			tTranVec[idx].rotation.z = pCompMesh->m_transforms[idx].m_rotation(2);
			tTranVec[idx].rotation.w = pCompMesh->m_transforms[idx].m_rotation(3);
		}
		pData->chunkTransforms = tTranVec;

		vector<bhkCMSDMaterial > tMtrlVec(pCompMesh->m_materials.getSize());

		for (unsigned int idx(0); idx < pCompMesh->m_materials.getSize(); ++idx)
		{
			bhkCMSDMaterial& material = tMtrlVec[idx];
			material.material = materials[pCompMesh->m_materials[idx]];
			material.filter.layer_sk = SKYL_STATIC;
		}

		//  set material list
		pData->chunkMaterials = tMtrlVec;

		vector<bhkCMSDChunk> chunkListNif(pCompMesh->m_chunks.getSize());

		//  for each chunk
		for (hkArray<hkpCompressedMeshShape::Chunk>::iterator pCIterHvk = pCompMesh->m_chunks.begin(); pCIterHvk != pCompMesh->m_chunks.end(); pCIterHvk++)
		{
			//  get nif chunk
			bhkCMSDChunk&	chunkNif = chunkListNif[chunkIdxNif];

			//  set offset => translation
			chunkNif.translation.x = pCIterHvk->m_offset(0);
			chunkNif.translation.y = pCIterHvk->m_offset(1);
			chunkNif.translation.z = pCIterHvk->m_offset(2);
			chunkNif.translation.w = pCIterHvk->m_offset(3);

			//  force flags to fixed values
			chunkNif.materialIndex = pCIterHvk->m_materialInfo;
			chunkNif.reference = 65535;
			chunkNif.transformIndex = pCIterHvk->m_transformIndex;

			//  vertices
			chunkNif.numVertices = pCIterHvk->m_vertices.getSize();
			chunkNif.vertices.resize(chunkNif.numVertices);
			for (unsigned int i(0); i < chunkNif.numVertices; ++i)
			{
				chunkNif.vertices[i] = pCIterHvk->m_vertices[i];
			}

			//  indices
			chunkNif.numIndices = pCIterHvk->m_indices.getSize();
			chunkNif.indices.resize(chunkNif.numIndices);
			for (unsigned int i(0); i < chunkNif.numIndices; ++i)
			{
				chunkNif.indices[i] = pCIterHvk->m_indices[i];
			}

			//  strips
			chunkNif.numStrips = pCIterHvk->m_stripLengths.getSize();
			chunkNif.strips.resize(chunkNif.numStrips);
			for (unsigned int i(0); i < chunkNif.numStrips; ++i)
			{
				chunkNif.strips[i] = pCIterHvk->m_stripLengths[i];
			}

			chunkNif.weldingInfo.resize(pCIterHvk->m_weldingInfo.getSize());
			for (int k = 0; k < pCIterHvk->m_weldingInfo.getSize(); k++) {
				chunkNif.weldingInfo[k] = pCIterHvk->m_weldingInfo[k];
			}

			++chunkIdxNif;

		}

		//  set modified chunk list to compressed mesh shape data
		pData->chunks = chunkListNif;
		//----  Merge  ----  END
	}
};

bhkShapeRef FBXWrangler::convert_from_hk(const hkpShape* shape)
{
	double bhkScaleFactorInverse = 1 / 69.99124908;
	if (shape == NULL)
	{
		throw runtime_error("Trying to convert unexistant shape! Abort");
		return NULL;
	}

	//Containers
	/// hkpListShape type.
	if (HK_SHAPE_LIST == shape->getType())
	{
		bhkListShapeRef list = new bhkListShape();
		hkpListShape* hk_list = (hkpListShape*)&*shape;
		size_t num_shapes = hk_list->getNumChildShapes();
		
		vector<unsigned int > keys;
		vector<bhkShapeRef> shapes;
		for (int i=0; i< num_shapes; i++)
			shapes.push_back(convert_from_hk(hk_list->getChildShapeInl(i)));

		hkpShapeKey key = hk_list->getFirstKey();
		while (key != HK_INVALID_SHAPE_KEY)
		{
			keys.push_back(key);
			key = hk_list->getNextKey(key);
		}
		list->SetSubShapes(shapes);
		Accessor<KeySetter>(list, keys);
		return StaticCast<bhkShape>(list);
	}
	/// hkpConvexTransformShape type.
	if (HK_SHAPE_CONVEX_TRANSFORM == shape->getType())
	{
		bhkConvexTransformShapeRef convex_transform = new bhkConvexTransformShape();
		return StaticCast<bhkShape>(convex_transform);
	}
	/// hkpTransformShape type.
	if (HK_SHAPE_TRANSFORM == shape->getType())
	{
		bhkTransformShapeRef transform = new bhkTransformShape();
		hkpTransformShape* hk_transform = (hkpTransformShape*)&*shape;
		transform->SetShape(convert_from_hk(hk_transform->getChildShape()));
		transform->SetTransform(TOMATRIX44(hk_transform->getTransform(), bhkScaleFactorInverse));
		return StaticCast<bhkShape>(transform);
	}
	/// hkpMoppBvTreeShape type.
	if (HK_SHAPE_MOPP == shape->getType())
	{
		bhkMoppBvTreeShapeRef pMoppShape = new bhkMoppBvTreeShape();
		hkpMoppBvTreeShape* pMoppBvTree = (hkpMoppBvTreeShape*)&*shape;

		pMoppShape->SetShape(convert_from_hk(pMoppBvTree->getShapeCollection()));
		pMoppShape->SetOrigin(Vector3(pMoppBvTree->getMoppCode()->m_info.m_offset(0), pMoppBvTree->getMoppCode()->m_info.m_offset(1), pMoppBvTree->getMoppCode()->m_info.m_offset(2)));
		pMoppShape->SetScale(pMoppBvTree->getMoppCode()->m_info.getScale());
		pMoppShape->SetBuildType(MoppDataBuildType((Niflib::byte) pMoppBvTree->getMoppCode()->m_buildType));
		pMoppShape->SetMoppData(vector<Niflib::byte>(pMoppBvTree->m_moppData, pMoppBvTree->m_moppData + pMoppBvTree->m_moppDataSize));

		return StaticCast<bhkShape>(pMoppShape);
	}

	//Shapes
	/// hkpSphereShape type.
	if (HK_SHAPE_SPHERE == shape->getType())
	{
		bhkSphereShapeRef sphere = new bhkSphereShape();
		return StaticCast<bhkShape>(sphere);
	}
	/// hkpBoxShape type.
	if (HK_SHAPE_BOX == shape->getType())
	{
		bhkBoxShapeRef box = new bhkBoxShape();
		hkpBoxShape* hk_box = (hkpBoxShape*)&*shape;
		box->SetDimensions(TOVECTOR3(hk_box->getHalfExtents(), bhkScaleFactorInverse));
		box->SetRadius(hk_box->getRadius());
		return StaticCast<bhkShape>(box);
	}
	/// hkpCapsuleShape type.
	if (HK_SHAPE_CAPSULE == shape->getType())
	{
		bhkCapsuleShapeRef capsule = new bhkCapsuleShape();
		hkpCapsuleShape* hk_capsule = (hkpCapsuleShape*)&*shape;
		capsule->SetFirstPoint(TOVECTOR3(hk_capsule->getVertices()[0], bhkScaleFactorInverse));
		capsule->SetSecondPoint(TOVECTOR3(hk_capsule->getVertices()[1], bhkScaleFactorInverse));
		hkVector4 direction; direction.setSub4(hk_capsule->getVertices()[1], hk_capsule->getVertices()[0]);
		capsule->SetRadius(hk_capsule->getRadius());
		capsule->SetRadius1(hk_capsule->getRadius());
		capsule->SetRadius2(hk_capsule->getRadius());
		return StaticCast<bhkShape>(capsule);
	}
	/// hkpConvexVerticesShape type.
	if (HK_SHAPE_CONVEX_VERTICES == shape->getType())
	{
		bhkConvexVerticesShapeRef convex = new bhkConvexVerticesShape();
		hkpConvexVerticesShape* hk_convex = (hkpConvexVerticesShape*)&*shape;
		hkArray<hkVector4> hk_vertices;
		vector<Vector4> vertices;
		hk_convex->getOriginalVertices(hk_vertices);
		for (const auto& v : hk_vertices)
		{
			vertices.push_back(TOVECTOR4(v, bhkScaleFactorInverse));
		}
		convex->SetVertices(vertices);
		const hkArray<hkVector4>& hk_planes = hk_convex->getPlaneEquations();
		vector<Vector4> planes;
		for (const auto& n : hk_planes)
		{
			planes.push_back(TOVECTOR4(n));
		}
		convex->SetNormals(planes);
		return StaticCast<bhkShape>(convex);
	}
	/// hkpCompressedMeshShape type.
	if (HK_SHAPE_COMPRESSED_MESH == shape->getType())
	{
		bhkCompressedMeshShapeRef mesh = new bhkCompressedMeshShape();
		bhkCompressedMeshShapeDataRef data = new bhkCompressedMeshShapeData();
		hkpCompressedMeshShape* hk_mesh = (hkpCompressedMeshShape*)&*shape;
		vector<SkyrimHavokMaterial> materials;
		Accessor<BCMSPacker> go(hk_mesh, data, materials);
		mesh->SetRadius(hk_mesh->m_radius);
		mesh->SetRadiusCopy(hk_mesh->m_radius);
		mesh->SetData(data);
		return StaticCast<bhkShape>(mesh);
	}

	throw runtime_error("Trying to convert unsupported shape type! Abort");
	return NULL;
}


NiCollisionObjectRef FBXWrangler::build_physics(FbxNode* rigid_body, set<pair<FbxAMatrix, FbxMesh*>>& geometry_meshes)
{
	NiCollisionObjectRef return_collision = NULL;
	string name = rigid_body->GetName();
	if (name.find("_rb") != string::npos)
	{
		bhkCollisionObjectRef collision = new bhkCollisionObject();
		bhkRigidBodyRef body = new bhkRigidBodyT();
		FbxAMatrix transform = rigid_body->EvaluateGlobalTransform();
		FbxVector4 T = transform.GetT();
		FbxQuaternion Q = transform.GetQ();
		Niflib::hkQuaternion q;
		q.x = (float)Q[0];
		q.y = (float)Q[1];
		q.z = (float)Q[2];
		q.w = (float)Q[3];
		float bhkScaleFactorInverse = 1.f / 69.99124908f;
		body->SetTranslation({ (float)T[0]*bhkScaleFactorInverse,(float)T[1] * bhkScaleFactorInverse, (float)T[2] * bhkScaleFactorInverse });
		body->SetRotation(q);
		body->SetShape(convert_from_hk(HKXWrapper::build_shape(rigid_body->GetChild(0), geometry_meshes)));
		collision->SetBody(StaticCast<bhkWorldObject>(body));
		return_collision = StaticCast<NiCollisionObject>(collision);
	}
	else if (name.find("_sp") != string::npos) {
		bhkSPCollisionObjectRef phantom_collision = new bhkSPCollisionObject();
		bhkSimpleShapePhantomRef phantom = new bhkSimpleShapePhantom();
		phantom->SetShape(convert_from_hk(HKXWrapper::build_shape(rigid_body->GetChild(0), geometry_meshes)));
		phantom_collision->SetBody(StaticCast<bhkWorldObject>(phantom));
		return_collision = StaticCast<NiCollisionObject>(phantom_collision);
	}
	else
		throw runtime_error("Unknown rigid body syntax!");

	return_collision->SetTarget(DynamicCast<NiAVObject>(conversion_Map[rigid_body->GetParent()]));

	return return_collision;
}

void FBXWrangler::buildCollisions()
{
	multimap<FbxNode*, FbxMesh*> bodies_meshes_map;

	std::function<void(FbxNode*)> findShapesToBeCollisioned = [&](FbxNode* root) {
		//recurse until we find a shape, avoiding rb as their mesh will be taken into account later
		string name = root->GetName();
		if (ends_with(name, "_rb") || ends_with(name, "_sp"))
			return;
		for (int i = 0; i < root->GetChildCount(); i++) {
			FbxNode* child = root->GetChild(i);

			findShapesToBeCollisioned(root->GetChild(i));
		}
		if (root->GetNodeAttribute() != NULL &&
			root->GetNodeAttribute()->GetAttributeType() == FbxNodeAttribute::eMesh)
		{
			//I'm a mesh, find my nearest parent RB, if any
			FbxNode* parent = root->GetParent();
			FbxNode* body = NULL;
			while (parent != NULL)
			{
				for (int i = 0; i < parent->GetChildCount(); i++) {
					FbxNode* child = parent->GetChild(i);
					string child_name = child->GetName();
					if (ends_with(child_name, "_rb") || ends_with(child_name, "_sp"))
					{
						body = child;
						break;
					}
				}
				if (body != NULL)
					break;
				parent = parent->GetParent();
			}


			bodies_meshes_map.insert({ body,
				(FbxMesh*)root->GetNodeAttribute() });
		}

	};

	findShapesToBeCollisioned(scene->GetRootNode());

	for (const auto& rb : physic_entities)
	{
		pair<multimap<FbxNode*, FbxMesh*>::iterator, std::multimap<FbxNode*, FbxMesh*>::iterator> this_body_meshes;
		this_body_meshes = bodies_meshes_map.equal_range(rb);
		FbxNode* this_body_parent = rb->GetParent();
		set<pair<FbxAMatrix, FbxMesh*>> meshes;
		for (multimap<FbxNode*, FbxMesh*>::iterator it = this_body_meshes.first; it != this_body_meshes.second; ++it)
		{
			FbxAMatrix transform;
			FbxNode* mesh_parent = it->second->GetNode();
			while (mesh_parent != NULL && mesh_parent != this_body_parent)
			{
				transform = mesh_parent->EvaluateLocalTransform() * transform;
				mesh_parent = mesh_parent->GetParent();
			}
			meshes.insert({ transform, it->second });
		}
		NiAVObjectRef ni_parent = DynamicCast<NiAVObject>(conversion_Map[rb->GetParent()]);
		ni_parent->SetCollisionObject(build_physics(rb, meshes));
	}
}

bool FBXWrangler::LoadMeshes(const FBXImportOptions& options) {
	if (!scene)
		return false;

	//Split meshes before starting

	FbxGeometryConverter lConverter(sdkManager);
	lConverter.SplitMeshesPerMaterial(scene, true);
	lConverter.Triangulate(scene, true);

	set<FbxNode*> bones;
	set<NiAVObjectRef> deferred_skins;
	if (export_skin)
		bones = buildBonesList();

	FbxNode* root = scene->GetRootNode();
	if (export_skin)
		conversion_root = new NiNode();
	else
		conversion_root = new BSFadeNode();
	conversion_root->SetName(string("Scene"));
	
	//nodes
	std::function<void(FbxNode*)> loadNodeChildren = [&](FbxNode* root) {
		NiNodeRef parent = DynamicCast<NiNode>(conversion_Map[root]);
		for (int i = 0; i < root->GetChildCount(); i++) {
			FbxNode* child = root->GetChild(i);
			NiAVObjectRef nif_child = NULL;
			if (child->GetNodeAttribute() != NULL && child->GetNodeAttribute()->GetAttributeType() == FbxNodeAttribute::eMesh) {
				nif_child = StaticCast<NiAVObject>(importShapes(child, options));
				setAvTransform(child, nif_child);
			}
			string child_name = child->GetName();
			if (ends_with(child_name,"_rb") || ends_with(child_name, "_sp"))
			{
				//collisions are leaves, defer them and return
				physic_entities.insert(child);
				continue;
			}
			if (nif_child == NULL) {
				nif_child = new NiNode();
				nif_child->SetName(unsanitizeString(string(child->GetName())));
				setAvTransform(child, nif_child);
			}
			conversion_Map[child] = nif_child;
			if (export_skin)
				parent = conversion_root;
			if (parent != NULL) {
				vector<Ref<NiAVObject > > children = parent->GetChildren();
				if (export_skin)
				{
					if (nif_child->IsDerivedType(NiNode::TYPE))
					{
						NiNodeRef node_child = DynamicCast<NiNode>(nif_child);
						vector<Ref<NiAVObject > > to_add_children = node_child->GetChildren();
						for (const auto& ref : to_add_children) {
							if (ref->IsDerivedType(NiTriBasedGeom::TYPE)) {
								//defer
								deferred_skins.insert(ref);
								//children.push_back(ref);
							}
						}
					}
					if (bones.find(child) != bones.end())
					{
						setAvTransform(child, nif_child, false, true);
						children.push_back(nif_child);
					}
				}
				else {
					children.push_back(nif_child);
				}
				parent->SetChildren(children);
				conversion_parent_Map[StaticCast<NiAVObject>(nif_child)] = StaticCast<NiAVObject>(parent);
			}

			loadNodeChildren(child);
		}
	};

	if (!hasNoTransform(root)) {
		NiNodeRef proxyNiNode = new NiNode();
		setAvTransform(root, proxyNiNode);
		proxyNiNode->SetName(string("rootTransformProxy"));
		conversion_root->SetChildren({ StaticCast<NiAVObject>(proxyNiNode) });
		conversion_Map[root] = proxyNiNode;
	}
	else {
		conversion_Map[root] = conversion_root;
	}
	loadNodeChildren(root);

	//skins
	for (const auto& p : skins_Map)
	{
		convertSkins(p.first, p.second);
	}

	//insertion deferred for skins
	if (export_skin)
	{
		vector<Ref<NiAVObject > > children = conversion_root->GetChildren();
		for (const auto& skin_def : deferred_skins)
		{
			children.push_back(skin_def);
		}
		conversion_root->SetChildren(children);
	}

	//animations
	size_t stacks = scene->GetSrcObjectCount<FbxAnimStack>();
	if (stacks > 0)
	{
		checkAnimatedNodes();
		//now we have the list of both skinned and unskinned bones and skinned and unskinned stacks of the FBX
		if (unskinned_animations.size() > 0)
			buildKF();
		if (skinned_animations.size() > 0) {

			if (external_skeleton_path.empty())
			{
				//build a skeleton, it must be complete.
				set<FbxNode*> skeleton;
				for (FbxNode* bone : skinned_bones)
				{
					FbxNode* current_node = bone;
					do {
						skeleton.insert(current_node);
						current_node = current_node->GetParent();
					} while (current_node != NULL && current_node != root);
				}
				//get back the ordered skeleton
				vector<FbxNode*> hkskeleton = hkxWrapper.create_skeleton("skeleton", skeleton);
				//create the sequences
				havok_sequences = hkxWrapper.create_animations("skeleton", hkskeleton, skinned_animations, scene->GetGlobalSettings().GetTimeMode());
			}
			else {

				string external_name;
				string external_root;
				vector<string> external_floats;
				vector<string> external_bones = hkxWrapper.read_track_list(external_skeleton_path, external_name, external_root, external_floats);
				//maya is picky about names, and stuff may be very well sanitized! especially skeeltons, which use an illegal syntax in Skyrim

				vector<FbxNode*> tracks;
				vector<uint32_t> transformTrackToBoneIndices;
				vector<FbxProperty> floats;
				vector<uint32_t> transformTrackToFloatIndices;

				//TODO: nif importer from max actually screwed up a lot of things,
				//we must also check for uppercase, lowercases and so on
				for (int i = 0; i < external_bones.size(); i++)
				{
					FbxNode* track = scene->FindNodeByName(external_bones[i].c_str());
					if (track == NULL)
					{
						//try uppercase, due to nif exporter changing that
						string uppercase;
						transform(external_bones[i].begin(), external_bones[i].end(), back_inserter(uppercase), toupper);
						track = scene->FindNodeByName(uppercase.c_str());
					}
					if (track == NULL)
					{
						string sanitized = external_bones[i];
						sanitizeString(sanitized);
						track = scene->FindNodeByName(sanitized.c_str());
					}
					if (track == NULL)
					{
						Log::Info("Track not present: %s", external_bones[i].c_str());
						continue;
					}
					//OPTIMIZATION: if track curve exists but doesn't have key, do not add
					if (animated_nodes.find(track) == animated_nodes.end())
						continue;
					transformTrackToBoneIndices.push_back(i);
					tracks.push_back(track);

				}
				if (external_bones.size() == tracks.size())
					//found all the bones, needs no mapping
					transformTrackToBoneIndices.clear();

				for (int i = 0; i < external_floats.size(); i++)
				{
					string track_name = external_floats[i];
					int index = track_name.find(":");

					if (index != string::npos)
					{
						string parent = track_name.substr(index + 1, track_name.size());
						track_name = track_name.substr(0, index);
						for (const auto& this_prop : float_properties)
						{
							if (track_name == this_prop.GetNameAsCStr())
							{
								transformTrackToFloatIndices.push_back(i);
								floats.push_back(this_prop);
								break;
							}
						}
					}
				}
				if (external_floats.size() == floats.size())
					//found all the tracks, needs no mapping
					transformTrackToFloatIndices.clear();

				havok_sequences = hkxWrapper.create_animations
				(
					external_name, 
					tracks, 
					skinned_animations, 
					scene->GetGlobalSettings().GetTimeMode(),
					transformTrackToBoneIndices,
					annotated,
					floats,
					transformTrackToFloatIndices
				);
			}
		}
	}

	//collisions
	buildCollisions();

	return true;
}

bool FBXWrangler::SaveAnimation(const string& fileName) {
	hkxWrapper.write_animations(fileName, havok_sequences);
	return true;
}

bool FBXWrangler::SaveSkin(const string& fileName) {
	export_skin = true;
	SaveNif(fileName);
	export_skin = false;
	return true;
}

bool FBXWrangler::SaveNif(const string& fileName) {

	NifInfo info;
	info.userVersion = 12;
	info.userVersion2 = 83;
	info.version = Niflib::VER_20_2_0_7;

	vector<NiObjectRef> objects = RebuildVisitor(conversion_root, info).blocks;
	bsx_flags_t calculated_flags = calculateSkyrimBSXFlags(objects, info);

	if (!export_skin)
	{
		//adjust for havok
		if (!skinned_animations.empty())
			calculated_flags[0] = true;

		BSXFlagsRef bref = new BSXFlags();
		bref->SetName(string("BSX"));
		bref->SetIntegerData(calculated_flags.to_ulong());

		conversion_root->SetExtraDataList({ StaticCast<NiExtraData>(bref) });
	}
	HKXWrapperCollection wrappers;

	if (!unskinned_animations.empty() || !skinned_animations.empty()) {
		fs::path in_file = fs::path(fileName).filename();
		string out_name = in_file.filename().replace_extension("").string();
		fs::path out_path = fs::path("animations") / in_file.parent_path() / out_name;
		fs::path out_path_abs = fs::path(fileName).parent_path() / out_path;
		string out_path_a = out_path_abs.string();
		string out_havok_path = skinned_animations.empty()?
			wrappers.wrap(out_name, out_path.parent_path().string(), out_path_a, "FBX", sequences_names):
			hkxWrapper.write_project(out_name, out_path.parent_path().string(), out_path_a, "FBX", sequences_names, havok_sequences);
		vector<Ref<NiExtraData > > list = conversion_root->GetExtraDataList();
		BSBehaviorGraphExtraDataRef havokp = new BSBehaviorGraphExtraData();
		havokp->SetName(string("BGED"));
		havokp->SetBehaviourGraphFile(out_havok_path);
		havokp->SetControlsBaseSkeleton(false);
		list.insert(list.begin(), StaticCast<NiExtraData>(havokp));
		conversion_root->SetExtraDataList(list);
	}

	NifFile out(info, objects);
	return out.Save(fileName);
}

vector<FbxNode*> FBXWrangler::importExternalSkeleton(const string& external_skeleton_path, vector<FbxProperty>& float_tracks)
{	
	this->external_skeleton_path = external_skeleton_path;
	return hkxWrapper.load_skeleton(external_skeleton_path, scene->GetRootNode(), float_tracks);
}

void FBXWrangler::importAnimationOnSkeleton(const string& external_skeleton_path, vector<FbxNode*>& skeleton, vector<FbxProperty>& float_tracks)
{
	hkxWrapper.load_animation(external_skeleton_path, skeleton, float_tracks);
}